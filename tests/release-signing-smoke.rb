#!/usr/bin/env ruby

workflow = File.read(ARGV.fetch(0))

required_fragments = {
  "certificate secret" => "APPLE_CERTIFICATE_P12_BASE64",
  "certificate import" => "security import",
  "macOS base64 decoding" => "base64 -D",
  "release signing identity" => "FOMOSNAP_CODESIGN_IDENTITY",
  "hardened runtime" => "--options runtime",
  "notarytool submission" => "xcrun notarytool submit",
  "notarytool key id" => "--key-id",
  "notarytool issuer" => "--issuer",
  "stapled ticket" => "xcrun stapler staple",
  "Gatekeeper verification" => "spctl --assess --type execute",
}

missing = required_fragments.filter_map do |label, fragment|
  "#{label} (#{fragment})" unless workflow.include?(fragment)
end

if missing.any?
  warn "Release signing workflow is missing:"
  warn missing.join("\n")
  exit 1
end

puts "Release signing workflow checks passed"
