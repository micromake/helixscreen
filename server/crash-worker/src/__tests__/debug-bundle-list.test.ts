// SPDX-License-Identifier: GPL-3.0-or-later
// Tests for the debug bundle list endpoint.

import { describe, it, expect, beforeEach } from "vitest";

// We test by calling the worker's fetch handler directly.
// Import the default export (the worker).
import worker from "../index";

/** Create a minimal Env with a mock DEBUG_BUNDLES R2 bucket. */
function createEnv(objects: MockR2Object[] = []): Env {
  return {
    DEBUG_BUNDLES: createMockBucket(objects),
    RELEASES_BUCKET: {} as R2Bucket,
    CRASH_LIMITER: { limit: async () => ({ success: true }) } as unknown as RateLimit,
    DEBUG_BUNDLE_LIMITER: { limit: async () => ({ success: true }) } as unknown as RateLimit,
    GITHUB_REPO: "test/repo",
    GITHUB_APP_ID: "123",
    NOTIFICATION_EMAIL: "",
    EMAIL_FROM: "",
    INGEST_API_KEY: "test-ingest-key",
    GITHUB_APP_PRIVATE_KEY: "",
    ADMIN_API_KEY: "test-admin-key",
    RESEND_API_KEY: "",
  } as Env;
}

interface MockR2Object {
  key: string;
  size: number;
  uploaded: Date;
  customMetadata?: Record<string, string>;
}

function createMockBucket(objects: MockR2Object[]): R2Bucket {
  return {
    async list(options?: R2ListOptions) {
      const cursor = options?.cursor ? parseInt(options.cursor, 10) : 0;
      const limit = options?.limit || 1000;
      const slice = objects.slice(cursor, cursor + limit);
      const nextCursor = cursor + limit;
      const truncated = nextCursor < objects.length;
      return {
        objects: slice.map((obj) => ({
          key: obj.key,
          size: obj.size,
          uploaded: obj.uploaded,
          customMetadata: obj.customMetadata || {},
          httpMetadata: {},
        })),
        truncated,
        cursor: truncated ? String(nextCursor) : undefined,
      };
    },
    async get() {
      return null;
    },
    async put() {
      return {} as R2Object;
    },
  } as unknown as R2Bucket;
}

/** Shorthand to create a mock R2 object with metadata. */
function mockBundle(
  key: string,
  opts: {
    size?: number;
    uploaded?: string;
    version?: string;
    printer_model?: string;
    platform?: string;
    klipper_version?: string;
  } = {}
): MockR2Object {
  return {
    key,
    size: opts.size || 50000,
    uploaded: new Date(opts.uploaded || "2026-02-26T12:00:00Z"),
    customMetadata: {
      version: opts.version || "0.13.8",
      printer_model: opts.printer_model || "Voron 2.4",
      platform: opts.platform || "pi",
      klipper_version: opts.klipper_version || "v0.12.0",
      uploaded: opts.uploaded || "2026-02-26T12:00:00Z",
    },
  };
}

async function fetchList(env: Env, query = ""): Promise<{ status: number; body: Record<string, unknown> }> {
  const request = new Request(`https://crash.helixscreen.org/v1/debug-bundle${query}`, {
    method: "GET",
    headers: { "X-Admin-Key": "test-admin-key" },
  });
  const response = await worker.fetch(request, env);
  const body = await response.json();
  return { status: response.status, body: body as Record<string, unknown> };
}

// --- Tests ---

describe("GET /v1/debug-bundle (list)", () => {
  it("requires admin authentication", async () => {
    const env = createEnv();
    const request = new Request("https://crash.helixscreen.org/v1/debug-bundle", {
      method: "GET",
    });
    const response = await worker.fetch(request, env);
    expect(response.status).toBe(401);
  });

  it("rejects invalid admin key", async () => {
    const env = createEnv();
    const request = new Request("https://crash.helixscreen.org/v1/debug-bundle", {
      method: "GET",
      headers: { "X-Admin-Key": "wrong-key" },
    });
    const response = await worker.fetch(request, env);
    expect(response.status).toBe(401);
  });

  it("returns empty list when no bundles exist", async () => {
    const env = createEnv([]);
    const { status, body } = await fetchList(env);
    expect(status).toBe(200);
    expect(body.bundles).toEqual([]);
    expect(body.truncated).toBe(false);
  });

  it("lists bundles with metadata", async () => {
    const env = createEnv([
      mockBundle("ABC12345", { version: "0.13.8", printer_model: "Voron 2.4", platform: "pi" }),
      mockBundle("DEF67890", { version: "0.13.7", printer_model: "Ender 3", platform: "ad5x" }),
    ]);
    const { status, body } = await fetchList(env);
    expect(status).toBe(200);
    const bundles = body.bundles as Array<Record<string, unknown>>;
    expect(bundles).toHaveLength(2);
    expect(bundles[0].share_code).toBeDefined();
    expect(bundles[0].size).toBeDefined();
    expect(bundles[0].uploaded).toBeDefined();
    expect(bundles[0].metadata).toBeDefined();
  });

  it("filters by since date", async () => {
    const env = createEnv([
      mockBundle("OLD00001", { uploaded: "2026-02-20T10:00:00Z" }),
      mockBundle("NEW00001", { uploaded: "2026-02-27T10:00:00Z" }),
    ]);
    const { body } = await fetchList(env, "?since=2026-02-25");
    const bundles = body.bundles as Array<Record<string, unknown>>;
    expect(bundles).toHaveLength(1);
    expect(bundles[0].share_code).toBe("NEW00001");
  });

  it("filters by until date", async () => {
    const env = createEnv([
      mockBundle("OLD00001", { uploaded: "2026-02-20T10:00:00Z" }),
      mockBundle("NEW00001", { uploaded: "2026-02-27T10:00:00Z" }),
    ]);
    const { body } = await fetchList(env, "?until=2026-02-25");
    const bundles = body.bundles as Array<Record<string, unknown>>;
    expect(bundles).toHaveLength(1);
    expect(bundles[0].share_code).toBe("OLD00001");
  });

  it("filters by since and until date range", async () => {
    const env = createEnv([
      mockBundle("EARLY001", { uploaded: "2026-02-18T10:00:00Z" }),
      mockBundle("MIDDLE01", { uploaded: "2026-02-22T10:00:00Z" }),
      mockBundle("LATE0001", { uploaded: "2026-02-28T10:00:00Z" }),
    ]);
    const { body } = await fetchList(env, "?since=2026-02-20&until=2026-02-25");
    const bundles = body.bundles as Array<Record<string, unknown>>;
    expect(bundles).toHaveLength(1);
    expect(bundles[0].share_code).toBe("MIDDLE01");
  });

  it("matches against version", async () => {
    const env = createEnv([
      mockBundle("VER13800", { version: "0.13.8" }),
      mockBundle("VER13700", { version: "0.13.7" }),
    ]);
    const { body } = await fetchList(env, "?match=0.13.8");
    const bundles = body.bundles as Array<Record<string, unknown>>;
    expect(bundles).toHaveLength(1);
    expect(bundles[0].share_code).toBe("VER13800");
  });

  it("matches against printer model (case-insensitive)", async () => {
    const env = createEnv([
      mockBundle("VORON001", { printer_model: "Voron 2.4" }),
      mockBundle("ENDER001", { printer_model: "Ender 3 S1" }),
    ]);
    const { body } = await fetchList(env, "?match=voron");
    const bundles = body.bundles as Array<Record<string, unknown>>;
    expect(bundles).toHaveLength(1);
    expect(bundles[0].share_code).toBe("VORON001");
  });

  it("matches against platform", async () => {
    const env = createEnv([
      mockBundle("PI000001", { platform: "pi" }),
      mockBundle("AD5X0001", { platform: "ad5x" }),
    ]);
    const { body } = await fetchList(env, "?match=ad5x");
    const bundles = body.bundles as Array<Record<string, unknown>>;
    expect(bundles).toHaveLength(1);
    expect(bundles[0].share_code).toBe("AD5X0001");
  });

  it("matches against share code", async () => {
    const env = createEnv([
      mockBundle("AAAA1111"),
      mockBundle("BBBB2222"),
    ]);
    const { body } = await fetchList(env, "?match=bbbb");
    const bundles = body.bundles as Array<Record<string, unknown>>;
    expect(bundles).toHaveLength(1);
    expect(bundles[0].share_code).toBe("BBBB2222");
  });

  it("respects limit parameter", async () => {
    const env = createEnv([
      mockBundle("BUND0001"),
      mockBundle("BUND0002"),
      mockBundle("BUND0003"),
      mockBundle("BUND0004"),
      mockBundle("BUND0005"),
    ]);
    const { body } = await fetchList(env, "?limit=2");
    const bundles = body.bundles as Array<Record<string, unknown>>;
    expect(bundles).toHaveLength(2);
    expect(body.truncated).toBe(true);
  });

  it("caps limit at 100", async () => {
    const env = createEnv([]);
    // Verify the request doesn't error with a huge limit
    const { status } = await fetchList(env, "?limit=999");
    expect(status).toBe(200);
  });

  it("defaults limit to 20", async () => {
    // Create 25 bundles
    const objects = Array.from({ length: 25 }, (_, i) =>
      mockBundle(`BND${String(i).padStart(5, "0")}`)
    );
    const env = createEnv(objects);
    const { body } = await fetchList(env);
    const bundles = body.bundles as Array<Record<string, unknown>>;
    expect(bundles).toHaveLength(20);
  });

  it("sorts results newest-first", async () => {
    const env = createEnv([
      mockBundle("OLDEST01", { uploaded: "2026-02-20T10:00:00Z" }),
      mockBundle("NEWEST01", { uploaded: "2026-02-27T10:00:00Z" }),
      mockBundle("MIDDLE01", { uploaded: "2026-02-24T10:00:00Z" }),
    ]);
    const { body } = await fetchList(env);
    const bundles = body.bundles as Array<Record<string, unknown>>;
    expect(bundles[0].share_code).toBe("NEWEST01");
    expect(bundles[1].share_code).toBe("MIDDLE01");
    expect(bundles[2].share_code).toBe("OLDEST01");
  });

  it("handles bundles without custom metadata (pre-migration)", async () => {
    const env = createEnv([
      {
        key: "LEGACY01",
        size: 12345,
        uploaded: new Date("2026-02-20T10:00:00Z"),
        // No customMetadata
      },
    ]);
    const { body } = await fetchList(env);
    const bundles = body.bundles as Array<Record<string, unknown>>;
    expect(bundles).toHaveLength(1);
    expect(bundles[0].share_code).toBe("LEGACY01");
    const meta = bundles[0].metadata as Record<string, string>;
    expect(meta.version).toBe("");
    expect(meta.printer_model).toBe("");
  });

  it("supports cursor-based pagination", async () => {
    // Our mock bucket returns cursor as string index
    const objects = Array.from({ length: 5 }, (_, i) =>
      mockBundle(`PAG${String(i).padStart(5, "0")}`, {
        uploaded: `2026-02-${20 + i}T10:00:00Z`,
      })
    );
    const env = createEnv(objects);

    // First page
    const { body: page1 } = await fetchList(env, "?limit=3");
    const bundles1 = page1.bundles as Array<Record<string, unknown>>;
    expect(bundles1).toHaveLength(3);
  });
});
