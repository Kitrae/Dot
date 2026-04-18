#!/usr/bin/env node

import fs from 'node:fs';
import fsp from 'node:fs/promises';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import {
  CallToolRequestSchema,
  ErrorCode,
  JSONRPCMessageSchema,
  ListPromptsRequestSchema,
  ListResourcesRequestSchema,
  ListResourceTemplatesRequestSchema,
  ListToolsRequestSchema,
  McpError,
} from '@modelcontextprotocol/sdk/types.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const LOG_PATH = path.join(__dirname, 'DotMcpServer.log');
const SERVER_VERSION = '0.1.0';
const MAX_MESSAGE_BYTES = 8 * 1024 * 1024;

function appendLog(message) {
  try {
    fs.appendFileSync(LOG_PATH, `${message}\n`);
  } catch {
    // stdout is reserved for MCP; ignore logging failures.
  }
}

function trimLineEndings(value) {
  return value.replace(/[\r\n]+$/g, '');
}

function normalizePath(value) {
  return value.replace(/\\/g, '/').toLowerCase();
}

function isProcessAlive(pid) {
  try {
    process.kill(pid, 0);
    return true;
  } catch {
    return false;
  }
}

function getLocalAppDataPath() {
  return process.env.LOCALAPPDATA || os.tmpdir();
}

function getInstancesDirectory() {
  return path.join(getLocalAppDataPath(), 'DotEngine', 'mcp', 'instances');
}

function parseArgs(argv) {
  const options = {
    pid: undefined,
    projectPath: undefined,
    showHelp: false,
  };

  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (argument === '--help' || argument === '-h') {
      options.showHelp = true;
    } else if (argument === '--pid' && index + 1 < argv.length) {
      options.pid = Number.parseInt(argv[index + 1], 10);
      index += 1;
    } else if (argument === '--project' && index + 1 < argv.length) {
      options.projectPath = argv[index + 1];
      index += 1;
    }
  }

  return options;
}

function printHelp() {
  process.stderr.write('DotMcpServer\n');
  process.stderr.write('  --pid <number>     Attach to a specific editor process id\n');
  process.stderr.write('  --project <path>   Attach to the editor instance for a specific project path\n');
}

async function discoverManifest(options) {
  const instancesDirectory = getInstancesDirectory();
  let entries = [];
  try {
    entries = await fsp.readdir(instancesDirectory, { withFileTypes: true });
  } catch {
    throw new Error('No running editor instances have published an MCP manifest yet.');
  }

  const candidates = [];
  for (const entry of entries) {
    if (!entry.isFile() || path.extname(entry.name) !== '.json') {
      continue;
    }

    try {
      const payload = await fsp.readFile(path.join(instancesDirectory, entry.name), 'utf8');
      const manifest = JSON.parse(payload);
      if (!manifest.bridgeEnabled) {
        continue;
      }
      if (!Number.isInteger(manifest.pid) || !manifest.pipeName || !manifest.projectPath) {
        continue;
      }
      if (!isProcessAlive(manifest.pid)) {
        continue;
      }
      if (options.pid !== undefined && manifest.pid !== options.pid) {
        continue;
      }
      if (
        options.projectPath !== undefined &&
        normalizePath(manifest.projectPath) !== normalizePath(path.resolve(options.projectPath))
      ) {
        continue;
      }
      candidates.push(manifest);
    } catch {
      // Ignore malformed or stale manifests.
    }
  }

  if (candidates.length === 0) {
    throw new Error('No running Dot Editor instance with the Toolbox MCP Bridge enabled was found.');
  }

  candidates.sort((left, right) => Number(right.processStartTime || 0) - Number(left.processStartTime || 0));
  return candidates[0];
}

function connectPipe(pipeName) {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection(pipeName);
    const onError = (error) => {
      socket.removeListener('connect', onConnect);
      reject(error);
    };
    const onConnect = () => {
      socket.removeListener('error', onError);
      socket.setNoDelay(true);
      resolve(socket);
    };

    socket.once('error', onError);
    socket.once('connect', onConnect);
  });
}

class BridgeClient {
  constructor(socket) {
    this.socket = socket;
    this.readBuffer = Buffer.alloc(0);
    this.pending = new Map();
    this.nextRequestId = 1;
    this.closed = false;
    this.onToolsChanged = undefined;

    socket.on('data', (chunk) => this.handleData(chunk));
    socket.on('error', (error) => this.handleClose(error));
    socket.on('close', () => this.handleClose(new Error('Bridge connection closed.')));
    socket.on('end', () => this.handleClose(new Error('Bridge connection ended.')));
  }

  handleClose(error) {
    if (this.closed) {
      return;
    }
    this.closed = true;
    appendLog(`bridge closed: ${error?.message ?? 'unknown'}`);
    for (const { reject } of this.pending.values()) {
      reject(error);
    }
    this.pending.clear();
  }

  handleData(chunk) {
    this.readBuffer = Buffer.concat([this.readBuffer, chunk]);

    while (this.readBuffer.length >= 4) {
      const length = this.readBuffer.readUInt32LE(0);
      if (length > MAX_MESSAGE_BYTES) {
        this.handleClose(new Error('Bridge message exceeds 8MB safety limit'));
        return;
      }

      if (this.readBuffer.length < 4 + length) {
        return;
      }

      const payloadBuffer = this.readBuffer.subarray(4, 4 + length);
      this.readBuffer = this.readBuffer.subarray(4 + length);

      let message;
      try {
        message = JSON.parse(payloadBuffer.toString('utf8'));
      } catch (error) {
        this.handleClose(error);
        return;
      }

      if (message.kind === 'response' && typeof message.id === 'string') {
        const pending = this.pending.get(message.id);
        if (pending) {
          this.pending.delete(message.id);
          pending.resolve(message);
        }
        continue;
      }

      if (message.kind === 'event' && message.name === 'tools_changed') {
        this.onToolsChanged?.();
      }
    }
  }

  async sendFrame(frame) {
    if (this.closed) {
      throw new Error('Bridge connection is closed.');
    }

    const payload = Buffer.from(JSON.stringify(frame), 'utf8');
    const header = Buffer.allocUnsafe(4);
    header.writeUInt32LE(payload.length, 0);
    const packet = Buffer.concat([header, payload]);

    await new Promise((resolve, reject) => {
      this.socket.write(packet, (error) => {
        if (error) {
          reject(error);
        } else {
          resolve();
        }
      });
    });
  }

  async call(method, params = {}) {
    if (this.closed) {
      throw new Error('Bridge connection is closed.');
    }

    const id = String(this.nextRequestId++);
    const responsePromise = new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
    });

    await this.sendFrame({
      kind: 'request',
      id,
      method,
      params,
    });

    return responsePromise;
  }

  close() {
    this.closed = true;
    this.socket.destroy();
  }
}

class ContentLengthStdioServerTransport {
  constructor(stdin = process.stdin, stdout = process.stdout) {
    this.stdin = stdin;
    this.stdout = stdout;
    this.readBuffer = Buffer.alloc(0);
    this.started = false;
    this.messageFraming = null;
    this.onData = (chunk) => {
      appendLog(`stdio chunk bytes=${chunk.length}`);
      this.readBuffer = Buffer.concat([this.readBuffer, chunk]);
      this.processReadBuffer();
    };
    this.onStreamError = (error) => {
      this.onerror?.(error);
    };
  }

  async start() {
    if (this.started) {
      throw new Error('ContentLengthStdioServerTransport already started.');
    }
    this.started = true;
    this.stdin.on('data', this.onData);
    this.stdin.on('error', this.onStreamError);
  }

  readMessage() {
    if (this.readBuffer.length === 0) {
      return null;
    }

    const previewLength = Math.min(this.readBuffer.length, 64);
    const preview = this.readBuffer.toString('utf8', 0, previewLength);
    const appearsContentLength = /^content-length:/i.test(preview);

    if (appearsContentLength) {
      this.messageFraming ??= 'content-length';
      const headerDelimiter = this.readBuffer.indexOf('\r\n\r\n');
      const altDelimiter = this.readBuffer.indexOf('\n\n');
      let delimiterIndex = headerDelimiter;
      let delimiterLength = 4;

      if (delimiterIndex === -1 || (altDelimiter !== -1 && altDelimiter < delimiterIndex)) {
        delimiterIndex = altDelimiter;
        delimiterLength = 2;
      }

      if (delimiterIndex === -1) {
        return null;
      }

      const headerText = this.readBuffer.toString('ascii', 0, delimiterIndex);
      const lines = headerText.split(/\r?\n/);
      let contentLength = undefined;
      for (const line of lines) {
        const match = /^content-length:\s*(\d+)$/i.exec(trimLineEndings(line));
        if (match) {
          contentLength = Number.parseInt(match[1], 10);
          break;
        }
      }

      if (!Number.isInteger(contentLength)) {
        throw new Error('Missing Content-Length header.');
      }
      if (contentLength > MAX_MESSAGE_BYTES) {
        throw new Error('Stdio message exceeds 8MB safety limit.');
      }

      const messageStart = delimiterIndex + delimiterLength;
      const messageEnd = messageStart + contentLength;
      if (this.readBuffer.length < messageEnd) {
        return null;
      }

      const payload = this.readBuffer.toString('utf8', messageStart, messageEnd);
      this.readBuffer = this.readBuffer.subarray(messageEnd);
      appendLog(`stdio recv: ${payload}`);
      return JSONRPCMessageSchema.parse(JSON.parse(payload));
    }

    const newlineIndex = this.readBuffer.indexOf('\n');
    if (newlineIndex === -1) {
      return null;
    }

    const line = this.readBuffer.toString('utf8', 0, newlineIndex).replace(/\r$/, '');
    this.readBuffer = this.readBuffer.subarray(newlineIndex + 1);
    if (line.trim().length === 0) {
      return null;
    }

    this.messageFraming ??= 'newline';
    appendLog(`stdio recv: ${line}`);
    return JSONRPCMessageSchema.parse(JSON.parse(line));
  }

  processReadBuffer() {
    while (true) {
      try {
        const message = this.readMessage();
        if (message === null) {
          return;
        }
        this.onmessage?.(message);
      } catch (error) {
        this.onerror?.(error instanceof Error ? error : new Error(String(error)));
        return;
      }
    }
  }

  async send(message) {
    appendLog(`stdio send: ${JSON.stringify(message)}`);
    const payload = Buffer.from(JSON.stringify(message), 'utf8');
    const useNewlineFraming = this.messageFraming === 'newline';
    const packet = useNewlineFraming
      ? Buffer.concat([payload, Buffer.from('\n', 'ascii')])
      : Buffer.concat([Buffer.from(`Content-Length: ${payload.length}\r\n\r\n`, 'ascii'), payload]);
    await new Promise((resolve, reject) => {
      this.stdout.write(packet, (error) => {
        if (error) {
          reject(error);
        } else {
          resolve();
        }
      });
    });
  }

  async close() {
    this.stdin.off('data', this.onData);
    this.stdin.off('error', this.onStreamError);
    this.readBuffer = Buffer.alloc(0);
    this.onclose?.();
  }
}

function toMcpError(error) {
  if (error instanceof McpError) {
    return error;
  }
  const message = error instanceof Error ? error.message : String(error);
  return new McpError(ErrorCode.InternalError, message);
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  if (options.showHelp) {
    printHelp();
    return;
  }

  appendLog('startup');
  const manifest = await discoverManifest(options);
  appendLog(`manifest pid=${manifest.pid} pipe=${manifest.pipeName}`);

  const bridge = new BridgeClient(await connectPipe(manifest.pipeName));
  const server = new Server(
    {
      name: 'DotMcpServer',
      title: 'Dot Editor MCP Server',
      version: SERVER_VERSION,
    },
    {
      capabilities: {
        logging: {},
        prompts: { listChanged: false },
        resources: { listChanged: false, subscribe: false },
        tools: { listChanged: true },
      },
      instructions:
        'Use tools to inspect and operate the running Dot Editor instance. Resources and prompts are intentionally empty.',
    },
  );

  bridge.onToolsChanged = () => {
    server.sendToolListChanged().catch((error) => {
      appendLog(`tool list changed notify failed: ${error instanceof Error ? error.message : String(error)}`);
    });
  };

  server.setRequestHandler(ListResourcesRequestSchema, async () => ({ resources: [] }));
  server.setRequestHandler(ListResourceTemplatesRequestSchema, async () => ({ resourceTemplates: [] }));
  server.setRequestHandler(ListPromptsRequestSchema, async () => ({ prompts: [] }));

  server.setRequestHandler(ListToolsRequestSchema, async () => {
    try {
      const response = await bridge.call('bridge.list_tools', {});
      if (!response.ok) {
        throw new McpError(ErrorCode.InternalError, response.errorMessage || 'Failed to list Dot Editor tools.');
      }
      return response.result;
    } catch (error) {
      throw toMcpError(error);
    }
  });

  server.setRequestHandler(CallToolRequestSchema, async (request) => {
    try {
      const response = await bridge.call(request.params.name, request.params.arguments ?? {});
      if (response.ok) {
        return response.result;
      }

      return (
        response.result ?? {
          content: [{ type: 'text', text: response.errorMessage || 'Tool call failed.' }],
          structuredContent: {},
          isError: true,
        }
      );
    } catch (error) {
      throw toMcpError(error);
    }
  });

  const transport = new ContentLengthStdioServerTransport();
  transport.onerror = (error) => appendLog(`stdio error: ${error instanceof Error ? error.message : String(error)}`);
  transport.onclose = () => {
    appendLog('stdio closed');
    bridge.close();
  };

  const shutdown = async () => {
    appendLog('shutdown');
    bridge.close();
    await transport.close().catch(() => {});
    process.exit(0);
  };

  process.on('SIGINT', () => {
    void shutdown();
  });
  process.on('SIGTERM', () => {
    void shutdown();
  });

  await server.connect(transport);
}

main().catch((error) => {
  const message = error instanceof Error ? error.message : String(error);
  appendLog(`fatal: ${message}`);
  process.stderr.write(`${message}\n`);
  if (message.includes('No running Dot Editor instance')) {
    process.stderr.write('Open Toolbox in Dot Editor and enable the `integration.mcp_bridge` module.\n');
  }
  process.exit(1);
});
