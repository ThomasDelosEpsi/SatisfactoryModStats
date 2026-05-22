'use strict';

const { Router } = require('express');
const { requireWebhookAuth } = require('../middleware/auth');
const { processFactoryPayload } = require('../services/factoryMetrics');

const router = Router();

/**
 * POST /api/webhook/satisfactory
 *
 * Receives the 30-second factory snapshot from the C++ mod.
 * Auth is enforced by the middleware before any payload work begins.
 */
router.post(
  '/satisfactory',
  requireWebhookAuth,
  async (req, res) => {
    const result = await processFactoryPayload(req.body);

    if (!result.ok) {
      // The request was authenticated but the payload was malformed.
      return res.status(400).json({ error: result.reason });
    }

    // Respond quickly — the game server doesn't need data back, just
    // an acknowledgement so it knows the push succeeded.
    return res.status(200).json({ received: true });
  }
);

module.exports = router;
