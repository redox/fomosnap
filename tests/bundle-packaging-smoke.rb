#!/usr/bin/env ruby

plist = File.read(ARGV.fetch(0))
required = "<key>LSUIElement</key>\n  <true/>"

unless plist.include?(required)
  warn "FOMOsnap bundle must be Dockless with LSUIElement"
  exit 1
end

puts "Bundle packaging checks passed"
