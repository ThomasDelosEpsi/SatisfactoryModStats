'use strict';

const express = require('express');
const helmet  = require('helmet');
const morgan  = require('morgan');
const rateLimit = require('express-rate-limit');

const webhookRouter = require('./src/routes/webhook');

const app = express();

// ============================================================
// Proxy trust
// MUST be set before any middleware that reads req.ip.
// '1' means trust the first hop (Nginx Proxy Manager).
// This makes req.ip reflect the real client IP from
// X-Forwarded-For instead of the proxy's loopback address.
// ============================================================
app.set('trust proxy', 1);

// ============================================================
// Security headers
// Sets sensible HTTP response headers (HSTS, no-sniff, etc.).
// ============================================================
app.use(helmet());

// ============================================================
// HTTP access logging
// 'combined' emits Apache-style logs including the real client
// IP (resolved correctly because trust proxy is set above).
// In production you may want to pipe this to a log file.
// ============================================================
app.use(morgan(process.env.NODE_ENV === 'production' ? 'combined' : 'dev'));

// ============================================================
// Body parsing
// Limit size to 1 MB — a factory snapshot is a few KB at most.
// This prevents a malicious or misconfigured sender from
// exhausting memory with a huge body.
// ============================================================
app.use(express.json({ limit: '1mb' }));

// ============================================================
// Rate limiting
// The game server sends one request every 30 seconds.
// We allow a generous burst (20/minute) to tolerate server
// restarts or catch-up behaviour, while still blocking abuse.
// ============================================================
const webhookLimiter = rateLimit({
  windowMs: 60 * 1000,   // 1 minute
  max: 20,
  standardHeaders: true, // Return RateLimit-* headers
  legacyHeaders: false,
  message: { error: 'Too many requests.' },
});

// ============================================================
// Routes
// ============================================================
app.use('/api/webhook', webhookLimiter, webhookRouter);

// ============================================================
// 404 catch-all
// Any path not matched above returns a clean 404.
// ============================================================
app.use((_req, res) => {
  res.status(404).json({ error: 'Not found.' });
});

// ============================================================
// Global error handler
// Catches any error thrown (or passed via next(err)) by a
// route or middleware.  Logs internally, returns a safe
// generic message to the caller — never leaks stack traces.
// ============================================================
// eslint-disable-next-line no-unused-vars
app.use((err, _req, res, _next) => {
  console.error('[app] Unhandled error:', err);
  res.status(500).json({ error: 'Internal server error.' });
});

module.exports = app;
