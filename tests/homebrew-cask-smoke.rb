#!/usr/bin/env ruby

require "fileutils"
require "pathname"
require "tmpdir"

class Cask
  class << self
    attr_reader :definition

    def define(name, &block)
      @definition = new(name)
      @definition.instance_eval(&block)
    end
  end

  def initialize(name)
    @name = name
    @system_calls = []
    @test_root = Pathname(Dir.mktmpdir("fomosnap-cask"))
  end

  attr_reader :system_calls

  def appdir
    @test_root.join("Applications")
  end

  def set_test_root(root)
    @test_root = Pathname(root)
    @system_calls.clear
  end

  def system_command(command, args: [])
    @system_calls << [command.to_s, *args.map(&:to_s)]
  end

  def run_postflight
    instance_eval(&@postflight)
  end

  def postflight(&block)
    @postflight = block
  end

  def method_missing(_name, *_arguments)
    nil
  end

  def respond_to_missing?(_name, _include_private)
    true
  end
end

def cask(name, &block)
  Cask.define(name, &block)
end

formula_path = ARGV.fetch(0)
load formula_path
cask = Cask.definition

def assert_equal(expected, actual, message)
  return if expected == actual

  warn "#{message}: expected #{expected.inspect}, got #{actual.inspect}"
  exit 1
end

def run_case(name)
  Dir.mktmpdir("fomosnap-cask-case") do |root|
    old_home = ENV["HOME"]
    ENV["HOME"] = root
    yield Pathname(root)
  ensure
    ENV["HOME"] = old_home
  end
rescue StandardError => error
  warn "#{name}: #{error.class}: #{error.message}"
  exit 1
end

marker = Pathname("Library/Application Support/fomosnap/homebrew-agent-defaulted")
plist = Pathname("Library/LaunchAgents/com.fomosnap.FOMOsnap.agent.plist")

run_case("fresh install") do |root|
  cask.set_test_root(root)
  cask.run_postflight
  expected = [[(root/"Applications/FOMOsnap.app/Contents/MacOS/FOMOsnap").to_s,
               "--install-agent"]]
  assert_equal(expected, cask.system_calls,
               "fresh install should install the agent")
  assert_equal(true, File.file?(root/marker),
               "fresh install should record the default")
end

run_case("upgrade with agent") do |root|
  cask.set_test_root(root)
  FileUtils.mkdir_p(root/plist.dirname)
  FileUtils.touch(root/plist)
  FileUtils.mkdir_p(root/marker.dirname)
  FileUtils.touch(root/marker)

  cask.run_postflight
  expected = [[(root/"Applications/FOMOsnap.app/Contents/MacOS/FOMOsnap").to_s,
               "--install-agent"]]
  assert_equal(expected, cask.system_calls,
               "upgrade should reload an existing agent")
end

run_case("upgrade after opt-out") do |root|
  cask.set_test_root(root)
  FileUtils.mkdir_p(root/marker.dirname)
  FileUtils.touch(root/marker)

  cask.run_postflight
  assert_equal([], cask.system_calls,
               "upgrade should preserve an explicit opt-out")
end

puts "Homebrew cask lifecycle checks passed"
