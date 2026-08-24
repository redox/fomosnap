#!/usr/bin/env ruby

require "fileutils"
require "pathname"
require "tmpdir"

class Formula
  def initialize(root)
    @root = Pathname(root)
    @system_calls = []
  end

  attr_reader :system_calls

  def var
    @root.join("var")
  end

  def opt_bin
    @root.join("bin")
  end

  def system(*arguments)
    @system_calls << arguments.map(&:to_s)
  end

  def touch(path)
    FileUtils.touch(path.to_s)
  end

  class << self
    def test(&_block)
      nil
    end

    def method_missing(_name, *_arguments)
      nil
    end

    def respond_to_missing?(_name, _include_private)
      true
    end
  end
end

formula_path = ARGV.fetch(0)
load formula_path

def assert_equal(expected, actual, message)
  return if expected == actual

  warn "#{message}: expected #{expected.inspect}, got #{actual.inspect}"
  exit 1
end

def run_case(name)
  Dir.mktmpdir("fomosnap-formula") do |root|
    old_home = ENV["HOME"]
    ENV["HOME"] = root
    yield root
  ensure
    ENV["HOME"] = old_home
  end
rescue StandardError => error
  warn "#{name}: #{error.class}: #{error.message}"
  exit 1
end

marker = "var/fomosnap/agent-defaulted"
plist = "Library/LaunchAgents/com.fomosnap.FOMOsnap.agent.plist"

run_case("fresh install") do |root|
  formula = Fomosnap.new(root)
  formula.post_install
  expected_command = [File.join(root, "bin/fomosnap"), "--install-agent"]
  assert_equal([expected_command], formula.system_calls,
               "fresh install should install the agent")
  assert_equal(true, File.file?(File.join(root, marker)),
               "fresh install should record that the default was applied")
end

run_case("upgrade with agent") do |root|
  FileUtils.mkdir_p(File.join(root, File.dirname(marker)))
  FileUtils.touch(File.join(root, marker))
  FileUtils.mkdir_p(File.join(root, File.dirname(plist)))
  FileUtils.touch(File.join(root, plist))

  formula = Fomosnap.new(root)
  formula.post_install
  expected_command = [File.join(root, "bin/fomosnap"), "--install-agent"]
  assert_equal([expected_command], formula.system_calls,
               "upgrade should reload an existing agent")
end

run_case("upgrade after opt-out") do |root|
  FileUtils.mkdir_p(File.join(root, File.dirname(marker)))
  FileUtils.touch(File.join(root, marker))

  formula = Fomosnap.new(root)
  formula.post_install
  assert_equal([], formula.system_calls,
               "upgrade should not recreate an explicitly removed agent")
end

puts "Homebrew formula lifecycle checks passed"
