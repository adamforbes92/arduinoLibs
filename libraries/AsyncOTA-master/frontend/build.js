const fs = require("fs");
const path = require("path");
const zlib = require("zlib");
const htmlMinifier = require("html-minifier-terser").minify;

// Define paths
const inputPath = path.join(__dirname, "index.html");
const outputPath = path.join(__dirname, "../src/OtaHTML.h");

// Function to split buffer into 64-byte chunks
function splitIntoChunks(buffer, chunkSize) {
  let chunks = [];
  for (let i = 0; i < buffer.length; i += chunkSize) {
    chunks.push(buffer.slice(i, i + chunkSize));
  }
  return chunks;
}

(async function () {
  // Step 1: Read and transform HTML
  let indexHtml = fs.readFileSync(inputPath, "utf8").toString();

  // Inline the SparkMD5 script dynamically
  const sparkMD5Script = fs.readFileSync("./node_modules/spark-md5/spark-md5.min.js", "utf8");
  indexHtml = indexHtml.replace(
    /<!-- build:sparkmd5 -->\s*<script src="node_modules\/spark-md5\/spark-md5\.js"><\/script>/g,
    `<script>${sparkMD5Script}</script>`
  );

  // Step 2: Minify the result
  const minifiedHtml = await htmlMinifier(indexHtml, {
    collapseWhitespace: true,
    removeComments: true,
    removeAttributeQuotes: true,
    removeRedundantAttributes: true,
    removeScriptTypeAttributes: true,
    removeStyleLinkTypeAttributes: true,
    useShortDoctype: true,
    minifyCSS: true,
    minifyJS: true,
    shortAttributes: true,
    shortClassName: true,
  });

  let oldSize = (indexHtml.length / 1024).toFixed(2);
  let newSize = (minifiedHtml.length / 1024).toFixed(2);

  console.log(`[Minifier] Original: ${oldSize}KB | Minified: ${newSize}KB`);

  // Step 3: Gzip the result
  let gzippedHtml = zlib.gzipSync(minifiedHtml);

  // Step 4: Write the result to the output file
  // Recreate the WebSerialHTML.h file with the new gzipped content
  // the content is stored as a byte array split into 64 byte chunks to avoid issues with the IDE
  let content = `#ifndef OTA_HTML_H
#define OTA_HTML_H

#include <Arduino.h>

const uint8_t OTA_HTML[] PROGMEM = {\n`;

  // Split gzipped HTML into 64-byte chunks
  let chunks = splitIntoChunks(gzippedHtml, 64);
  chunks.forEach((chunk, index) => {
    content += `  ${Array.from(chunk)
      .map((byte) => `0x${byte.toString(16).padStart(2, "0")}`)
      .join(", ")}`;
    if (index < chunks.length - 1) {
      content += ",\n";
    }
  });

  content += `\n};

#endif // OTA_HTML_H`;

  // Write the content to the output file
  fs.writeFileSync(outputPath, content);

  console.log("OtaHTML.h file created successfully!");
})();
