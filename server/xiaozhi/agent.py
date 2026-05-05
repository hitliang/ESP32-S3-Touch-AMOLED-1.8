"""LLM Agent with tool calling for Reddy voice assistant."""

from __future__ import annotations

import json
import asyncio
import logging
from datetime import datetime
from openai import AsyncOpenAI

from .config import LLMConfig
from .memory import ConversationMemory

logger = logging.getLogger("xz.agent")

DEFAULT_SYSTEM_PROMPT = (
    "你是一个给6岁小男孩用的语音助手。"
    "男孩中文名叫瑞迪，英文名Reddy，上小学一年级。"
    "规则：用简单中文，句子短，适合6岁小孩。"
    "语气温暖耐心，不要讲可怕内容。回答简短，不超过三句话。"
    "如果你不知道答案，诚实地说不知道，不要编造。"
)

MAX_ITERATIONS = 10


class LLMClient:
    """Async LLM client via OpenAI-compatible API (DeepSeek)."""

    def __init__(self, config: LLMConfig):
        self.client = AsyncOpenAI(
            api_key=config.api_key,
            base_url=config.base_url,
        )
        self.model = config.model
        self.max_tokens = config.max_tokens
        self.temperature = config.temperature
        self._tool_schemas: list[dict] | None = None

    def set_tool_schemas(self, schemas: list[dict] | None):
        self._tool_schemas = schemas

    async def chat(self, messages: list[dict]) -> tuple[str | None, dict | None]:
        """Send messages to LLM. Returns (text_reply, tool_calls_dict) or (None, None)."""
        kwargs = dict(
            model=self.model,
            messages=messages,
            max_tokens=self.max_tokens,
            temperature=self.temperature,
            timeout=120,
        )
        if self._tool_schemas:
            kwargs["tools"] = self._tool_schemas
            kwargs["tool_choice"] = "auto"

        try:
            resp = await self.client.chat.completions.create(**kwargs)
        except Exception as e:
            logger.error(f"LLM call failed: {e}")
            return None, None

        choice = resp.choices[0]
        msg = choice.message

        # Tool calls?
        if choice.finish_reason == "tool_calls" and msg.tool_calls:
            tool_calls = {
                "calls": [
                    {
                        "id": tc.id,
                        "name": tc.function.name,
                        "arguments": tc.function.arguments,
                    }
                    for tc in msg.tool_calls
                ],
            }
            if hasattr(msg, "reasoning_content") and msg.reasoning_content:
                tool_calls["reasoning"] = msg.reasoning_content
            return None, tool_calls

        return msg.content or "", None

    async def chat_simple(
        self,
        system_prompt: str,
        user_message: str,
        max_tokens: int | None = None,
        temperature: float | None = None,
    ) -> str:
        """Simple single-turn chat without tool calling or conversation history."""
        messages = [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_message},
        ]
        kwargs = dict(
            model=self.model,
            messages=messages,
            max_tokens=max_tokens or self.max_tokens,
            temperature=temperature if temperature is not None else self.temperature,
            timeout=120,
        )
        try:
            resp = await self.client.chat.completions.create(**kwargs)
            return resp.choices[0].message.content or ""
        except Exception as e:
            logger.error(f"LLM simple chat failed: {e}")
            return ""


class ToolExecutor:
    """Registry of callable tools for the agent."""

    def __init__(self):
        self._tools: dict[str, dict] = {}

    def register(self, schema: dict, handler):
        name = schema["function"]["name"]
        self._tools[name] = {"schema": schema, "handler": handler}

    @property
    def schemas(self) -> list[dict] | None:
        if not self._tools:
            return None
        return [t["schema"] for t in self._tools.values()]

    async def execute(self, name: str, arguments: str) -> str:
        if name not in self._tools:
            return f"Unknown tool: {name}"
        try:
            args = json.loads(arguments) if arguments else {}
        except json.JSONDecodeError:
            args = {}
        logger.info(f"Tool: {name}({json.dumps(args, ensure_ascii=False)[:200]})")
        try:
            result = await self._tools[name]["handler"](**args)
            return result if isinstance(result, str) else json.dumps(result, ensure_ascii=False)
        except Exception as e:
            logger.error(f"Tool {name} error: {e}")
            return f"Error executing {name}: {e}"


class AgentEngine:
    """ReAct agent for voice assistant."""

    def __init__(
        self,
        llm: LLMClient,
        memory: ConversationMemory,
        tool_executor: ToolExecutor | None = None,
        system_prompt: str = "",
    ):
        self.llm = llm
        self.memory = memory
        self.tools = tool_executor or ToolExecutor()
        self.system_prompt = system_prompt or DEFAULT_SYSTEM_PROMPT
        self.llm.set_tool_schemas(self.tools.schemas)

    async def process(self, user_text: str) -> str:
        """Process a user message and return the assistant's reply."""
        # Save user message
        await self.memory.add("user", user_text)

        # Build messages for LLM using four-tier memory
        current_time = datetime.now().strftime("%Y年%m月%d日 %H:%M")
        messages = self.memory.build_context(self.system_prompt, current_time)

        # ReAct loop
        for iteration in range(MAX_ITERATIONS):
            text, tool_calls = await self.llm.chat(messages)

            if tool_calls:
                # Add assistant message with tool calls
                assistant_msg = {
                    "role": "assistant",
                    "content": None,
                    "tool_calls": [
                        {
                            "id": tc["id"],
                            "type": "function",
                            "function": {"name": tc["name"], "arguments": tc["arguments"]},
                        }
                        for tc in tool_calls["calls"]
                    ],
                }
                if "reasoning" in tool_calls:
                    assistant_msg["reasoning_content"] = tool_calls["reasoning"]
                messages.append(assistant_msg)

                # Execute tools
                for tc in tool_calls["calls"]:
                    result = await self.tools.execute(tc["name"], tc["arguments"])
                    messages.append({
                        "role": "tool",
                        "tool_call_id": tc["id"],
                        "content": result,
                    })
                continue

            # Text response
            if text:
                await self.memory.add("assistant", text)
                # Run memory maintenance (summary + fact extraction + profile update)
                await self.memory.maintenance(self.llm)
                return text

            # Empty response
            logger.warning(f"Agent iteration {iteration} returned empty")
            return "抱歉，我刚才没有想好怎么回答。"

        return "抱歉，我想了太久，让我们重新开始吧。"
