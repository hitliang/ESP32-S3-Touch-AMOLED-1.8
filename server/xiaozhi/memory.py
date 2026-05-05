"""Four-tier conversation memory for DeepSeek 1M context.

Tier 1: Recent messages — last N turns, always kept in full detail
Tier 2: Session summary — auto-condensed older conversation (triggers at 500K tokens)
Tier 3: Long-term facts — extracted discrete facts about the user (persisted across sessions)
Tier 4: User profile — structured JSON: name, age, interests, preferences, important dates

All tiers persisted via SQLite; facts and profile survive server restarts.
"""

from __future__ import annotations

import json
import logging
from datetime import datetime
from typing import List, Dict, Optional, Tuple

logger = logging.getLogger("xz.memory")

# ── Thresholds ──────────────────────────────────────────────────────
MAX_RECENT_TOKENS = 500_000         # trigger summarization when recent exceeds this
MAX_RECENT_MESSAGES = 100           # max recent turns to keep after summarization
KEEP_RECENT_AFTER_SUMMARY = 30      # keep this many most recent messages untouched
TOTAL_BUDGET_TOKENS = 900_000       # leave 100K for system prompt + response
FACT_EXTRACTION_INTERVAL = 10       # extract facts every N turns

# User profile template
DEFAULT_PROFILE = {
    "name": "瑞迪",
    "english_name": "Reddy",
    "age": 6,
    "grade": "小学一年级",
    "interests": [],
    "dislikes": [],
    "important_dates": [],
    "preferences": {},
    "notes": "",
    "last_updated": "",
}


def estimate_tokens(text: str) -> int:
    """Estimate token count for mixed Chinese/English text.
    Uses cl100k_base (GPT-4 / DeepSeek-like) tokenizer if available,
    falls back to ~0.7 chars/token for Chinese, ~0.25 for English.
    """
    try:
        import tiktoken
        enc = tiktoken.get_encoding("cl100k_base")
        return len(enc.encode(text))
    except (ImportError, Exception):
        # Fallback: rough heuristic for mixed Chinese text
        chars = len(text)
        # Chinese chars ~1.5 tokens/char, ASCII ~0.25 tokens/char
        ascii_count = sum(1 for c in text if ord(c) < 128)
        cjk_count = chars - ascii_count
        return int(ascii_count * 0.3 + cjk_count * 0.7)


class ConversationMemory:
    """Four-tier memory manager for a single user (Reddy)."""

    def __init__(self, db_path: str = "./data/reddy_memory.db"):
        self._db_path = db_path
        self._messages: List[Tuple[str, str, int]] = []  # (role, content, tokens)
        self._summary: str = ""
        self._summary_tokens: int = 0
        self._facts: List[Dict] = []          # [{"content":..., "category":..., "importance":...}]
        self._profile: Dict = dict(DEFAULT_PROFILE)
        self._total_recent_tokens: int = 0
        self._turn_count: int = 0

    # ── Database ─────────────────────────────────────────────────
    async def _get_db(self):
        import aiosqlite
        db = await aiosqlite.connect(self._db_path)
        await db.execute("PRAGMA journal_mode=WAL")
        await db.execute("PRAGMA synchronous=NORMAL")
        return db

    async def load_or_create(self):
        db = await self._get_db()
        try:
            await db.execute(
                "CREATE TABLE IF NOT EXISTS messages ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  role TEXT NOT NULL, content TEXT NOT NULL,"
                "  token_count INTEGER DEFAULT 0,"
                "  created_at TEXT DEFAULT (datetime('now'))"
                ")"
            )
            await db.execute(
                "CREATE TABLE IF NOT EXISTS session_summary ("
                "  id INTEGER PRIMARY KEY, content TEXT NOT NULL,"
                "  token_count INTEGER DEFAULT 0,"
                "  updated_at TEXT DEFAULT (datetime('now'))"
                ")"
            )
            await db.execute(
                "CREATE TABLE IF NOT EXISTS facts ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  content TEXT NOT NULL,"
                "  category TEXT DEFAULT 'general',"
                "  importance INTEGER DEFAULT 5,"
                "  created_at TEXT DEFAULT (datetime('now')),"
                "  updated_at TEXT DEFAULT (datetime('now'))"
                ")"
            )
            await db.execute(
                "CREATE TABLE IF NOT EXISTS user_profile ("
                "  id INTEGER PRIMARY KEY,"
                "  profile_json TEXT NOT NULL,"
                "  updated_at TEXT DEFAULT (datetime('now'))"
                ")"
            )
            await db.commit()

            # Load summary
            cursor = await db.execute("SELECT content, token_count FROM session_summary WHERE id=1")
            row = await cursor.fetchone()
            if row:
                self._summary = row[0]
                self._summary_tokens = row[1]

            # Load facts
            cursor = await db.execute(
                "SELECT content, category, importance FROM facts ORDER BY importance DESC, id DESC"
            )
            rows = await cursor.fetchall()
            self._facts = [
                {"content": r[0], "category": r[1], "importance": r[2]}
                for r in rows
            ]

            # Load profile
            cursor = await db.execute("SELECT profile_json FROM user_profile WHERE id=1")
            row = await cursor.fetchone()
            if row:
                saved = json.loads(row[0])
                self._profile.update(saved)

            # Load recent messages
            cursor = await db.execute("SELECT role, content, token_count FROM messages ORDER BY id ASC")
            rows = await cursor.fetchall()
            self._messages = [(r[0], r[1], r[2]) for r in rows]
            self._total_recent_tokens = sum(t for _, _, t in self._messages)
            self._turn_count = sum(1 for r, _, _ in self._messages if r == "user")

        finally:
            await db.close()

        logger.info(
            f"Memory: {len(self._messages)} msgs ({self._total_recent_tokens}t), "
            f"summary {self._summary_tokens}t, "
            f"{len(self._facts)} facts, profile_age={self._profile.get('age','?')}"
        )

    # ── Add messages ─────────────────────────────────────────────
    async def add(self, role: str, content: str):
        tokens = estimate_tokens(content)
        self._messages.append((role, content, tokens))
        self._total_recent_tokens += tokens
        if role == "user":
            self._turn_count += 1
        # Persist immediately so admin page sees it
        await self._insert_message(role, content, tokens)

    # ── Build LLM context ────────────────────────────────────────
    def build_context(self, system_prompt: str, current_time: str) -> list[dict]:
        """Build the full message list for LLM, respecting token budget."""
        messages: list[dict] = []

        # Layer 0: System prompt
        system_text = f"{system_prompt}\n\n当前时间: {current_time}"

        # Layer 4: User profile
        profile_text = self._format_profile()
        if profile_text:
            system_text += f"\n\n## 用户画像\n{profile_text}"

        # Layer 3: Long-term facts
        facts_text = self._format_facts()
        if facts_text:
            system_text += f"\n\n## 关于Reddy的重要信息\n{facts_text}"

        messages.append({"role": "system", "content": system_text})

        # Layer 2: Session summary
        if self._summary:
            messages.append({
                "role": "system",
                "content": f"[更早的对话摘要] {self._summary}",
            })

        # Layer 1: Recent messages (within budget)
        budget = TOTAL_BUDGET_TOKENS - estimate_tokens(system_text) - self._summary_tokens
        included = []
        included_tokens = 0
        # Walk from most recent backward
        for role, content, tokens in reversed(self._messages):
            if included_tokens + tokens > budget:
                break
            included.insert(0, {"role": role, "content": content})
            included_tokens += tokens

        if len(included) < len(self._messages):
            logger.info(
                f"Context: truncated {len(self._messages) - len(included)} old msgs, "
                f"kept {len(included)} ({included_tokens}t)"
            )

        messages.extend(included)
        return messages

    # ── Formatting helpers ───────────────────────────────────────
    def _format_profile(self) -> str:
        p = self._profile
        parts = [f"- 姓名: {p.get('name','')} ({p.get('english_name','')}), {p.get('age','')}岁, {p.get('grade','')}"]

        interests = p.get("interests", [])
        if interests:
            parts.append(f"- 兴趣爱好: {', '.join(interests)}")

        dislikes = p.get("dislikes", [])
        if dislikes:
            parts.append(f"- 不喜欢: {', '.join(dislikes)}")

        prefs = p.get("preferences", {})
        if prefs:
            pref_str = ", ".join(f"{k}={v}" for k, v in prefs.items())
            parts.append(f"- 偏好: {pref_str}")

        dates = p.get("important_dates", [])
        if dates:
            date_strs = [f"{d.get('event','')}({d.get('date','')})" for d in dates]
            parts.append(f"- 重要日期: {', '.join(date_strs)}")

        notes = p.get("notes", "")
        if notes:
            parts.append(f"- {notes}")

        return "\n".join(parts)

    def _format_facts(self) -> str:
        if not self._facts:
            return ""
        items = [f"- {f['content']}" for f in self._facts[:30]]  # cap at 30 facts
        return "\n".join(items)

    # ── Auto-summarization ───────────────────────────────────────
    async def maybe_summarize(self, llm):
        """Check if summarization is needed and run it (Tier 2)."""
        if self._total_recent_tokens < MAX_RECENT_TOKENS or len(self._messages) < 30:
            return

        logger.info(f"Summarizing: {len(self._messages)} msgs, {self._total_recent_tokens}t")

        # Keep last KEEP_RECENT_AFTER_SUMMARY messages as-is
        split = max(10, len(self._messages) - KEEP_RECENT_AFTER_SUMMARY)
        old = self._messages[:split]
        recent = self._messages[split:]

        # Build conversation text for summarization
        conv_text = ""
        for role, content, _ in old:
            conv_text += f"[{role}]: {content}\n"

        existing = f"之前的摘要: {self._summary}\n\n" if self._summary else ""
        prompt = (
            "请将以下对话历史压缩为一份结构化的摘要（300-800字），保留：\n"
            "1. 用户分享的个人信息和偏好\n"
            "2. 讨论的主要话题\n"
            "3. 用户表达的情感或态度\n"
            "4. 任何需要后续跟进的事项\n"
            "只输出摘要内容，不要加任何前缀或后缀。"
        )

        try:
            resp = await llm.chat_simple(
                system_prompt=prompt,
                user_message=f"{existing}对话记录:\n{conv_text[-8000:]}",  # limit for summarization
                max_tokens=1200,
                temperature=0.3,
            )
            if resp:
                self._summary = resp
                self._summary_tokens = estimate_tokens(resp)
                self._messages = recent
                self._total_recent_tokens = sum(t for _, _, t in recent)
                await self._persist_summary()
                await self._persist_messages()
                logger.info(
                    f"Summary: {len(resp)} chars, kept {len(recent)} msgs ({self._total_recent_tokens}t)"
                )
        except Exception as e:
            logger.error(f"Summarization failed: {e}")

    # ── Fact extraction ──────────────────────────────────────────
    async def maybe_extract_facts(self, llm):
        """Extract long-term facts from recent conversation (Tier 3)."""
        if self._turn_count % FACT_EXTRACTION_INTERVAL != 0 or self._turn_count == 0:
            return
        if len(self._messages) < 5:
            return

        # Use last 15 turns for extraction
        recent = self._messages[-30:]
        conv = ""
        for role, content, _ in recent:
            conv += f"[{role}]: {content}\n"

        prompt = (
            "从以下对话中提取关于用户（6岁男孩Reddy）的新信息。"
            "只提取客观事实，不要推测。输出JSON数组，每项包含：\n"
            "- content: 事实描述（简洁的一句话）\n"
            "- category: 分类（interest/personality/preference/event/family/other）\n"
            "- importance: 重要性1-10\n"
            "如果对话没有值得记录的新信息，返回空数组 []。"
        )

        try:
            resp = await llm.chat_simple(
                system_prompt=prompt,
                user_message=conv,
                max_tokens=1000,
                temperature=0.2,
            )
            if not resp:
                return
            # Parse JSON array from response
            data = self._parse_json_from_response(resp)
            if not data or not isinstance(data, list):
                return

            # Merge with existing facts (avoid duplicates, update conflicts)
            new_count = 0
            for item in data:
                content = item.get("content", "").strip()
                if not content or len(content) < 3:
                    continue
                # Check for similar existing fact
                if any(self._similar(content, f["content"]) for f in self._facts):
                    continue
                self._facts.append({
                    "content": content,
                    "category": item.get("category", "general"),
                    "importance": int(item.get("importance", 5)),
                })
                new_count += 1

            if new_count:
                # Sort by importance desc, trim to 50
                self._facts.sort(key=lambda f: f["importance"], reverse=True)
                if len(self._facts) > 50:
                    self._facts = self._facts[:50]
                await self._persist_facts()
                logger.info(f"Extracted {new_count} new facts (total {len(self._facts)})")

        except Exception as e:
            logger.error(f"Fact extraction failed: {e}")

    # ── Profile update ───────────────────────────────────────────
    async def maybe_update_profile(self, llm):
        """Periodically update the user profile from facts and recent conversation (Tier 4)."""
        if self._turn_count % 20 != 0 or self._turn_count == 0:
            return

        current_profile = json.dumps(self._profile, ensure_ascii=False)
        conv = ""
        for role, content, _ in self._messages[-20:]:
            conv += f"[{role}]: {content}\n"

        prompt = (
            "根据最新的对话，更新以下用户画像JSON。"
            "只修改有变化的字段，保持JSON格式完整。\n"
            "注意：interests是数组，去重合并；important_dates格式为[{\"event\":\"\",\"date\":\"\"}]。\n\n"
            f"当前画像: {current_profile}\n\n"
            "输出更新后的完整画像JSON，不要任何解释文字。"
        )

        try:
            resp = await llm.chat_simple(
                system_prompt=prompt,
                user_message=f"最新对话:\n{conv}",
                max_tokens=800,
                temperature=0.2,
            )
            if not resp:
                return
            new_profile = self._parse_json_from_response(resp)
            if not new_profile or not isinstance(new_profile, dict):
                return
            new_profile["last_updated"] = datetime.now().strftime("%Y-%m-%d")
            self._profile.update(new_profile)
            await self._persist_profile()
            logger.info("Profile updated")
        except Exception as e:
            logger.error(f"Profile update failed: {e}")

    # ── Full memory maintenance cycle ────────────────────────────
    async def maintenance(self, llm):
        """Run all memory maintenance tasks."""
        await self.maybe_summarize(llm)
        await self.maybe_extract_facts(llm)
        await self.maybe_update_profile(llm)

    # ── Utility ──────────────────────────────────────────────────
    @staticmethod
    def _similar(a: str, b: str, threshold: float = 0.6) -> bool:
        """Check if two strings are similar (simple char overlap)."""
        if a == b:
            return True
        sa, sb = set(a), set(b)
        if not sa or not sb:
            return False
        overlap = len(sa & sb) / min(len(sa), len(sb))
        return overlap > threshold

    @staticmethod
    def _parse_json_from_response(text: str):
        """Extract JSON from LLM response (may be wrapped in markdown or have extra text)."""
        text = text.strip()
        # Try direct parse
        try:
            return json.loads(text)
        except (json.JSONDecodeError, ValueError):
            pass
        # Try extracting from ```json ... ``` block
        if "```" in text:
            try:
                start = text.index("```") + 3
                if text[start:start + 4].lower() == "json":
                    start += 4
                end = text.index("```", start)
                return json.loads(text[start:end].strip())
            except (ValueError, json.JSONDecodeError):
                pass
        # Try finding first [ or {
        for bracket in ["[", "{"]:
            try:
                start = text.index(bracket)
                end = text.rindex("]" if bracket == "[" else "}")
                return json.loads(text[start:end + 1])
            except (ValueError, json.JSONDecodeError):
                pass
        return None

    # ── Persistence ──────────────────────────────────────────────
    async def _insert_message(self, role: str, content: str, tokens: int):
        db = await self._get_db()
        try:
            await db.execute(
                "INSERT INTO messages (role, content, token_count) VALUES (?, ?, ?)",
                (role, content, tokens),
            )
            await db.commit()
        finally:
            await db.close()

    async def _persist_summary(self):
        db = await self._get_db()
        try:
            await db.execute("DELETE FROM session_summary")
            await db.execute(
                "INSERT INTO session_summary (id, content, token_count) VALUES (1, ?, ?)",
                (self._summary, self._summary_tokens),
            )
            await db.commit()
        finally:
            await db.close()

    async def _persist_messages(self):
        db = await self._get_db()
        try:
            await db.execute("DELETE FROM messages")
            for role, content, tokens in self._messages:
                await db.execute(
                    "INSERT INTO messages (role, content, token_count) VALUES (?, ?, ?)",
                    (role, content, tokens),
                )
            await db.commit()
        finally:
            await db.close()

    async def _persist_facts(self):
        db = await self._get_db()
        try:
            await db.execute("DELETE FROM facts")
            for f in self._facts:
                await db.execute(
                    "INSERT INTO facts (content, category, importance) VALUES (?, ?, ?)",
                    (f["content"], f.get("category", "general"), f.get("importance", 5)),
                )
            await db.commit()
        finally:
            await db.close()

    async def _persist_profile(self):
        db = await self._get_db()
        try:
            profile_json = json.dumps(self._profile, ensure_ascii=False)
            await db.execute("DELETE FROM user_profile")
            await db.execute(
                "INSERT INTO user_profile (id, profile_json) VALUES (1, ?)",
                (profile_json,),
            )
            await db.commit()
        finally:
            await db.close()

    async def clear(self):
        self._messages = []
        self._summary = ""
        self._summary_tokens = 0
        self._facts = []
        self._profile = dict(DEFAULT_PROFILE)
        self._total_recent_tokens = 0
        self._turn_count = 0
        db = await self._get_db()
        try:
            for table in ["messages", "session_summary", "facts", "user_profile"]:
                await db.execute(f"DELETE FROM {table}")
            await db.commit()
        finally:
            await db.close()
