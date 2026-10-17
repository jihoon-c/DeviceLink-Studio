# DeviceLink Studio MCP Server

This stdio MCP server exposes the DeviceLink Application layer to an LLM host.
It starts the native `DeviceLinkAgentBridge.exe`; equipment TCP connections and
packet construction stay in the native layers. The optional LLM adapter uses HTTP
to a configured inference server.

## Build and run

```powershell
cmake --build build --config Release --target DeviceLinkAgentBridge
python mcp_server/server.py
```

If the executable is outside the default build directory, set
`DEVICELINK_AGENT_BRIDGE` to its absolute path.

Example MCP client configuration:

```json
{
  "mcpServers": {
    "devicelink": {
      "command": "python",
      "args": ["F:/GitHubRep/DeviceLinkStudio/mcp_server/server.py"]
    }
  }
}
```

Start the Unreal virtual device before calling `connect_device`.

## Local diagnostic LLM

Run `scripts/Setup-LocalDiagnosticLlm.ps1` from the repository root to install
the portable Ollama runtime and Qwen3 4B model. It writes the ignored
`mcp_server/llm.local.json`. After reboot, use the same script with `-SkipPull`.
Local calls use a placeholder key and never forward `OPENAI_API_KEY`.

The local model selects Korean procedures from cited sample manual sections;
unsupported procedures are rejected. Diagnoses and subsequent verifications are
saved under `artifacts/diagnostics`. `get_diagnosis_report` can read them after
restarting the MCP server.

See [local setup and validation](../docs/LOCAL_LLM_SETUP_KR.md) for commands,
the manual procedure format, and the scope of the tests.
