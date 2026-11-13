#!/usr/bin/env node
// Runs the Update page's image check (web/image_check.js) on the images test_image_check wrote, and
// compares each verdict with the C++ check's (lib/core image_identity). Used by that native test:
//   node tools/check_image_js.js <dir with manifest.json>
// Exit 0 if every verdict agrees.
"use strict";
const fs = require("fs");
const path = require("path");
const { judgeImage } = require(path.join(__dirname, "..", "web", "image_check.js"));

const dir = process.argv[2];
const manifest = JSON.parse(fs.readFileSync(path.join(dir, "manifest.json"), "utf8"));
let bad = 0;
for (const m of manifest) {
  const bytes = fs.readFileSync(path.join(dir, m.file));
  const got = judgeImage(bytes.subarray(0, 352)).code;
  if (got !== m.code) {
    bad++;
    console.log(`  DIFFER  ${m.file}: C++ ${m.code}, page ${got}`);
  }
}
console.log(`  ${manifest.length - bad} of ${manifest.length} images: the page and the board agree`);
process.exit(bad ? 1 : 0);
