"""Conversation memory with auto-summarization for Reddy voice assistant."""

from __future__ import annotations

import json
import logging
from datetime import datetime

logger = logging.getLogger("xz.memory")

SUMMARIZE_THRESHOLD = 50_000  # token estimate before summarization
KEEP_RECENT = 20  # keep most recent N messages after summarization
MAX_TOKENS = 200_000  # max tokens sent to LLM


class ConversationMemory:
    """In-memory conversation store with token estimation and auto-summarization.

    For the device use case, we use a single conversation (device_id = "reddy").
    Memory is persisted to SQLite but loaded into memory for fast access.
    """

    def __init__(self, db_path: str = "./data/reddy_memory.db"):
        self._db_path = db_path
        self._messages: list[dict] = []
        self._summary: str | None = None
        self._total_tokens = 0

    async def load_or_create(self):
        """Load messages from DB or initialize empty."""
        import aiosqlite
        async with aiosqlite.connect(self._db_path) as db:
            await db.execute(
                "CREATE TABLE IF NOT EXISTS messages ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  role TEXT NOT NULL,"
                "  content TEXT NOT NULL,"
                "  token_estimate INTEGER DEFAULT 0,"
                "  created_at TEXT DEFAULT (datetime('now'))"
                ")"
            )
            await db.execute(
                "CREATE TABLE IF NOT EXISTS summary ("
                "  id INTEGER PRIMARY KEY,"
                "  content TEXT NOT NULL,"
                "  token_estimate INTEGER DEFAULT 0,"
                "  updated_at TEXT DEFAULT (datetime('now'))"
                ")"
            )
            await db.commit()

            # Load summary
            cursor = await db.execute("SELECT content FROM summary WHERE id=1")
            row = await cursor.fetchone()
            if row:
                self._summary = row[0]

            # Load messages
            cursor = await db.execute(
                "SELECT role, content, token_estimate FROM messages ORDER BY id ASC"
            )
            rows = await cursor.fetchall()
            for role, content, tok in rows:
                self._messages.append({"role": role, "content": content})
                self._total_tokens += tok

        logger.info(f"Memory loaded: {len(self._messages)} messages, {self._total_tokens} tokens")

    def add(self, role: str, content: str):
        tok = max(1, len(content) // 2)
        self._messages.append({"role": role, "content": content})
        self._total_tokens += tok

    def get_messages_for_llm(self) -> list[dict]:
        """Build message list for LLM with optional summary prefix."""
        msgs: list[dict] = []
        if self._summary:
            msgs.append({"role": "system", "content": self._summary})
        msgs.extend(self._messages)
        return msgs

    def get_last_user_message(self) -> str:
        """Get the most recent user message."""
        for m in reversed(self._messages):
            if m["role"] == "user":
                return m["content"]
        return ""

    async def maybe_summarize(self, llm):
        """Check if summarization is needed and run it."""
        if self._total_tokens < SUMMARIZE_THRESHOLD or len(self._messages) < 30:
            return

        logger.info(f"Summarizing: {len(self._messages)} msgs, {self._total_tokens} tokens")

        # Keep last KEEP_RECENT messages, summarize the rest
        split = max(5, len(self._messages) - KEEP_RECENT)
        old_msgs = self._messages[:split]
        recent = self._messages[split:]

        # Build summarization prompt
        conv_text = ""
        for m in old_msgs:
            conv_text += f"[{m['role']}]: {m['content'][:300]}\n"

        prompt = (
            "请将以下对话历史压缩为一段简短的摘要（200-500字），"
            "保留关键信息：用户提到的重要事实、偏好、之前讨论的话题等。"
            "只输出摘要内容，不要加任何前缀或后缀。"
        )

        try:
            resp = await llm.chat_simple(
                system_prompt=prompt,
                user_message=conv_text,
                max_tokens=800,
                temperature=0.3,
            )
            if resp:
                self._summary = resp
                self._messages = recent
                self._total_tokens = sum(max(1, len(m["content"]) // 2) for m in recent)
                await self._persist_summary()
                await self._persist_messages(recent)
                logger.info(f"Summarized to {len(self._summary)} chars, kept {len(recent)} msgs")
        except Exception as e:
            logger.error(f"Summarization failed: {e}")

    async def _persist_summary(self):
        import aiosqlite
        async with aiosqlite.connect(self._db_path) as db:
            await db.execute("DELETE FROM summary")
            if self._summary:
                tok = max(1, len(self._summary) // 2)
                await db.execute(
                    "INSERT INTO summary (id, content, token_estimate) VALUES (1, ?, ?)",
                    (self._summary, tok),
                )
            await db.commit()

    async def _persist_messages(self, messages: list[dict]):
        import aiosqlite
        async with aiosqlite.connect(self._db_path) as db:
            await db.execute("DELETE FROM messages")
            for m in messages:
                tok = max(1, len(m["content"]) // 2)
                await db.execute(
                    "INSERT INTO messages (role, content, token_estimate) VALUES (?, ?, ?)",
                    (m["role"], m["content"], tok),
                )
            await db.commit()

    async def clear(self):
        self._messages = []
        self._summary = None
        self._total_tokens = 0
        import aiosqlite
        async with aiosqlite.connect(self._db_path) as db:
            await db.execute("DELETE FROM messages")
            await db.execute("DELETE FROM summary")
            await db.commit()
