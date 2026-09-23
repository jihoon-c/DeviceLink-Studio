# DeviceLink Studio MCP Server

This stdio MCP server exposes the DeviceLink Application layer to an LLM host.
It starts the native `DeviceLinkAgentBridge.exe`; the Python process does not
open sockets or construct device packets.

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
