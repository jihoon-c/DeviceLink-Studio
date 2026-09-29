@echo off
setlocal
cd /d "%~dp0"
py -3 mcp_server\natural_language_cli.py %*
