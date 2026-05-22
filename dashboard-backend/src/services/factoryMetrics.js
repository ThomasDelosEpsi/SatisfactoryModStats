'use strict';

// ============================================================
// factoryMetrics.js — Service layer for incoming factory data
//
// This file is the single place where you will inject database
// logic.  The route stays untouched; you only modify the
// functions below when you add persistence.
// ============================================================

/**
 * Shape of a validated factory payload (JSDoc for IDE autocomplete).
 *
 * @typedef {Object} FactoryPayload
 * @property {string}           timestamp       - ISO 8601 UTC timestamp.
 * @property {string}           schema_version  - Payload schema version.
 * @property {InventoryData}    inventory       - Container metrics.
 * @property {ProductionData}   production      - Building efficiency metrics.
 * @property {PowerData}        power           - Circuit power metrics.
 */

// ------------------------------------------------------------
// Validation
// ------------------------------------------------------------

/**
 * Performs a lightweight structural validation of the raw payload.
 * Rejects payloads that are missing required top-level keys so the
 * service never processes garbage data.
 *
 * @param {unknown} body - Raw parsed JSON from the request.
 * @returns {{ valid: boolean, reason?: string }}
 */
function validatePayload(body) {
  if (!body || typeof body !== 'object' || Array.isArray(body)) {
    return { valid: false, reason: 'Payload must be a JSON object.' };
  }

  const required = ['timestamp', 'inventory', 'production', 'power'];
  for (const key of required) {
    if (!(key in body)) {
      return { valid: false, reason: `Missing required field: "${key}".` };
    }
  }

  return { valid: true };
}

// ------------------------------------------------------------
// Sub-handlers (one per data domain)
// Each receives the relevant sub-object from the payload.
// Replace the console.log calls with your DB write logic.
// ------------------------------------------------------------

/**
 * Handles inventory data.
 * @param {FactoryPayload['inventory']} inventory
 * @param {string} timestamp
 */
async function handleInventory(inventory, timestamp) {
  console.log('[metrics:inventory]', {
    timestamp,
    total_items:     inventory.total_items,
    container_count: inventory.container_count,
  });

  // ── TODO: persist to database ─────────────────────────────
  // Example with a hypothetical db client:
  //
  // await db.query(
  //   `INSERT INTO inventory_snapshots (timestamp, total_items, container_count, containers_json)
  //    VALUES ($1, $2, $3, $4)`,
  //   [timestamp, inventory.total_items, inventory.container_count,
  //    JSON.stringify(inventory.containers)]
  // );
  // ──────────────────────────────────────────────────────────
}

/**
 * Handles production building data.
 * @param {FactoryPayload['production']} production
 * @param {string} timestamp
 */
async function handleProduction(production, timestamp) {
  console.log('[metrics:production]', {
    timestamp,
    total_buildings:     production.total_buildings,
    producing_buildings: production.producing_buildings,
    avg_efficiency_pct:  production.avg_efficiency_pct,
  });

  // ── TODO: persist to database ─────────────────────────────
  // await db.query(
  //   `INSERT INTO production_snapshots (timestamp, total_buildings, producing_buildings, avg_efficiency_pct, buildings_json)
  //    VALUES ($1, $2, $3, $4, $5)`,
  //   [timestamp, production.total_buildings, production.producing_buildings,
  //    production.avg_efficiency_pct, JSON.stringify(production.buildings)]
  // );
  // ──────────────────────────────────────────────────────────
}

/**
 * Handles power grid data.
 * @param {FactoryPayload['power']} power
 * @param {string} timestamp
 */
async function handlePower(power, timestamp) {
  console.log('[metrics:power]', {
    timestamp,
    total_produced_mw: power.total_produced_mw,
    total_consumed_mw: power.total_consumed_mw,
    circuit_count:     power.circuit_count,
    any_fused:         power.circuits?.some(c => c.is_fused) ?? false,
  });

  // ── TODO: persist to database ─────────────────────────────
  // await db.query(
  //   `INSERT INTO power_snapshots (timestamp, total_produced_mw, total_consumed_mw, circuit_count, circuits_json)
  //    VALUES ($1, $2, $3, $4, $5)`,
  //   [timestamp, power.total_produced_mw, power.total_consumed_mw,
  //    power.circuit_count, JSON.stringify(power.circuits)]
  // );
  // ──────────────────────────────────────────────────────────
}

// ------------------------------------------------------------
// Public entry point
// ------------------------------------------------------------

/**
 * Main service function called by the webhook route.
 * Validates, fans out to domain handlers, and returns a summary
 * the route can include in its 200 response.
 *
 * @param {unknown} rawBody - Parsed JSON body from Express.
 * @returns {Promise<{ ok: boolean, reason?: string }>}
 */
async function processFactoryPayload(rawBody) {
  const { valid, reason } = validatePayload(rawBody);
  if (!valid) {
    return { ok: false, reason };
  }

  /** @type {FactoryPayload} */
  const payload = rawBody;
  const { timestamp, inventory, production, power } = payload;

  // Run all domain handlers concurrently — if one fails it doesn't
  // block the others, and we surface the error without crashing.
  const results = await Promise.allSettled([
    handleInventory(inventory, timestamp),
    handleProduction(production, timestamp),
    handlePower(power, timestamp),
  ]);

  const failures = results
    .filter(r => r.status === 'rejected')
    .map(r => r.reason?.message ?? String(r.reason));

  if (failures.length > 0) {
    console.error('[metrics] One or more handlers failed:', failures);
    // Still return ok:true — the webhook was received correctly.
    // Partial failures are a persistence concern, not a transport concern.
  }

  return { ok: true };
}

module.exports = { processFactoryPayload };
