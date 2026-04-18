# DotMcpServer

`DotMcpServer` is the local stdio MCP shim for Dot Editor. It discovers the newest running editor instance with the Toolbox MCP bridge enabled, connects through the local named pipe bridge, and exposes MCP tools over stdio using the official `@modelcontextprotocol/sdk`.

Example command:

```powershell
node .\Tools\DotMcpServer\server.mjs
```

Target a specific editor instance:

```powershell
node .\Tools\DotMcpServer\server.mjs --pid 12345
```

Target a specific project:

```powershell
node .\Tools\DotMcpServer\server.mjs --project C:\Users\thurm\Documents\Kit3D\Apex
```

Example MCP client config snippet:

```json
{
  "mcpServers": {
    "dot-editor": {
      "command": "node",
      "args": [
        "C:\\Users\\thurm\\Documents\\Kit3D\\Apex\\build_fresh\\bin\\Debug\\DotMcpServer\\server.mjs"
      ]
    }
  }
}
```
