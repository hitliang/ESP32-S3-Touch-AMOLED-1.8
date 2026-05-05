#!/usr/bin/env python3
"""Entry point for Xiaozhi WebSocket server."""

import os
import sys
import logging
import asyncio
from dotenv import load_dotenv

# Load .env from current directory
load_dotenv()

from xiaozhi.config import load_config
from xiaozhi.server import XiaozhiServer


def setup_logging(level: str = "INFO"):
    logging.basicConfig(
        level=getattr(logging, level.upper(), logging.INFO),
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )


async def main():
    config = load_config()
    setup_logging(config.log_level)

    logger = logging.getLogger("xz")
    logger.info("Starting Reddy Bot Xiaozhi Server...")
    logger.info(f"LLM: {config.llm.model} @ {config.llm.base_url}")
    logger.info(f"Port: {config.server.port}")

    server = XiaozhiServer(config)
    try:
        await server.start()
    except KeyboardInterrupt:
        logger.info("Shutting down...")
    finally:
        await server.close()


if __name__ == "__main__":
    asyncio.run(main())
