'use strict';

const crypto = require('crypto');

/**
 * Compares two strings in constant time to prevent timing-based token
 * enumeration attacks.  Returns false immediately on length mismatch
 * (length itself is not sensitive for a fixed-format Bearer token).
 *
 * @param {string} provided  - Token extracted from the request header.
 * @param {string} expected  - Token from process.env.WEBHOOK_SECRET.
 * @returns {boolean}
 */
function timingSafeCompare(provided, expected) {
  try {
    const a = Buffer.from(provided, 'utf8');
    const b = Buffer.from(expected, 'utf8');
    if (a.length !== b.length) return false;
    return crypto.timingSafeEqual(a, b);
  } catch {
    return false;
  }
}

/**
 * Express middleware — validates the Authorization: Bearer <TOKEN> header
 * against the WEBHOOK_SECRET environment variable.
 *
 * Responds 401 on any failure so the caller learns nothing about *why*
 * the request was rejected (missing header, wrong token, bad format all
 * return the same error body).
 */
function requireWebhookAuth(req, res, next) {
  const secret = process.env.WEBHOOK_SECRET;

  // Fail closed: if the secret is not configured the server is
  // misconfigured — reject everything rather than accepting anything.
  if (!secret) {
    console.error('[auth] WEBHOOK_SECRET is not set — rejecting request.');
    return res.status(401).json({ error: 'Unauthorized' });
  }

  const authHeader = req.headers['authorization'] ?? '';
  const [scheme, token] = authHeader.split(' ');

  if (scheme !== 'Bearer' || !token) {
    return res.status(401).json({ error: 'Unauthorized' });
  }

  if (!timingSafeCompare(token, secret)) {
    // Log the source IP (correctly resolved behind Nginx thanks to
    // app.set('trust proxy', 1)) to help spot brute-force attempts.
    console.warn(`[auth] Invalid token from ${req.ip}`);
    return res.status(401).json({ error: 'Unauthorized' });
  }

  next();
}

module.exports = { requireWebhookAuth };
