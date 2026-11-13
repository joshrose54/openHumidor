/* The Update page's early check of a firmware image (spec 5.5): the same rules as the board's own
 * check, lib/core/src/wineador/image_identity.cpp, so the page can warn before anything is sent.
 * The board's check is the one that counts. test_image_check (native) runs this file under node on
 * the same images as the C++ check and fails if the two ever disagree.
 *
 * judgeImage(bytes) -> { ok, code, text, version }: code is the C++ imageVerdictCode(); version the
 * image's own (from its identity block; "" when it has none). */
"use strict";

const IMAGE_CHECK_BYTES = 352;          // kImageCheckBytes
const IDENTITY_OFFSET = 0x120;          // kIdentityOffset
const IDENTITY_MAGIC = "openHumidor-ID";

function judgeImage(b) {
  let version = "";
  const say = (code, text) => ({ ok: code === "accept", code, text, version });
  if (b.length < IMAGE_CHECK_BYTES) return say("too_short", "The file is too short to be a firmware image.");
  if (b[0] !== 0xE9) return say("not_firmware", "That file isn't a firmware image (use firmware.bin from the revq build).");
  const chip = b[12] | (b[13] << 8);
  if (chip === 0) return say("rev_n_image", "That is a Rev N image (ESP32): it can't run on this board.");
  if (chip !== 9) return say("wrong_chip", "That image is for another kind of chip.");
  if (!(b[0x20] === 0x32 && b[0x21] === 0x54 && b[0x22] === 0xCD && b[0x23] === 0xAB)) {
    return say("no_app_description", "That image isn't firmware built for this project.");
  }
  const txt = (o, n) => { let s = ""; for (let i = o; i < o + n && b[i] !== 0; i++) s += String.fromCharCode(b[i]); return s; };
  const id = IDENTITY_OFFSET;
  // the magic with its NUL, exactly
  let magic = true;
  for (let i = 0; i < IDENTITY_MAGIC.length; i++) if (b[id + i] !== IDENTITY_MAGIC.charCodeAt(i)) magic = false;
  if (!magic || b[id + IDENTITY_MAGIC.length] !== 0) {
    return say("no_identity", "That image carries no openHumidor identity: a BP2 test sketch, or firmware from before stage Q5.");
  }
  if (b[id + 33] !== 1) return say("unknown_identity_layout", "That image's identity is in a newer layout this page can't read.");
  version = txt(id + 36, 28);
  if (txt(id + 16, 8) !== "rev-q") return say("wrong_board", "That image is built for another board.");
  if (txt(id + 24, 8) !== "real") return say("bench_build", "That is a bench build (short timers): never for the cabinet.");
  if (b[id + 32] !== 0) return say("test_build", "That is a test build (scripted readings): never for the cabinet.");
  return say("accept", `A Rev Q cabinet build, version ${version}.`);
}

if (typeof module !== "undefined") module.exports = { judgeImage, IMAGE_CHECK_BYTES };
