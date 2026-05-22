'use strict';

// Load .env before anything else so all modules see the variables.
require('dotenv').config();

const app  = require('./app');

const PORT = parseInt(process.env.PORT ?? '3000', 10);

// Validate that the secret is configured before accepting connections.
// Fail fast rather than silently running in an insecure state.
if (!process.env.WEBHOOK_SECRET) {
  console.error('[server] FATAL: WEBHOOK_SECRET is not set in .env — refusing to start.');
  process.exit(1);
}

const server = app.listen(PORT, '127.0.0.1', () => {
  console.log(`[server] Listening on 127.0.0.1:${PORT} (${process.env.NODE_ENV ?? 'development'})`);
  console.log(`[server] Webhook endpoint: POST http://127.0.0.1:${PORT}/api/webhook/satisfactory`);
});

// ============================================================
// Graceful shutdown
// On SIGTERM (systemd stop) or SIGINT (Ctrl-C), stop accepting
// new connections and wait for in-flight requests to finish.
// ============================================================
function shutdown(signal) {
  console.log(`[server] ${signal} received — shutting down gracefully.`);
  server.close(() => {
    console.log('[server] All connections closed. Exiting.');
    process.exit(0);
  });

  // Force-kill after 10 s if connections stall.
  setTimeout(() => {
    console.error('[server] Forced exit after timeout.');
    process.exit(1);
  }, 10_000).unref();
}

process.on('SIGTERM', () => shutdown('SIGTERM'));
process.on('SIGINT',  () => shutdown('SIGINT'));
