---
name: rebase-upstream-macos
description: Rebase the FOMOsnap macOS fork onto the latest upstream omasnap main, adapt shared changes for macOS, run the complete checks, and open a pull request. Use when periodically syncing this fork with https://github.com/tobi/omasnap.
disable-model-invocation: true
---

# Rebase upstream omasnap onto the macOS fork

Run this workflow from the FOMOsnap repository root when the user explicitly
invokes this skill. The deliverable is a tested pull request against
`redox/fomosnap:main`; do not claim success until the PR exists.

## Non-negotiable constraints

- Read `AGENTS.md` before changing code and follow its macOS-only boundaries.
- Preserve the macOS platform implementation in `src/mac/*.mm` and keep Apple
  headers out of non-Objective-C++ files.
- Port compatible upstream changes to shared/editor code; do not reintroduce
  Wayland, Hyprland, X11, or Linux-only build paths.
- Never use `git reset --hard`, `git clean`, a force push, or an automatic stash.
- Do not open a PR when the build or tests fail.
- Keep unrelated local work out of the sync branch.

## 1. Preflight and safety gate

1. Confirm the current repository and read `AGENTS.md`.
2. Run:

   ```bash
   git status --short --branch
   git remote -v
   ```

   Require a clean working tree. If any tracked, staged, or untracked file is
   present, stop and report the exact paths. Do not stash, clean, reset, or
   otherwise alter those files.
3. Require an `upstream` remote whose fetch URL is
   `https://github.com/tobi/omasnap` (an equivalent `.git` suffix is fine).
   Require an `origin` remote pointing at the FOMOsnap fork. If either remote
   is missing or points elsewhere, stop and ask the user to fix it.
4. Fetch both branch tips without merging:

   ```bash
   git fetch --prune upstream main
   git fetch --prune origin main
   ```

5. If `HEAD` does not contain `origin/main`, stop rather than silently
   omitting fork commits. The user must first update the starting branch.
6. Record the fetched upstream commit:

   ```bash
   git rev-parse upstream/main
   ```

## 2. Create an isolated sync branch

Create a new branch from the clean current `HEAD`; never rebase the user's
existing branch in place:

```bash
git switch -c "sync/upstream-$(git rev-parse --short upstream/main)"
```

If that branch name already exists, append a unique suffix and create it
without replacing or checking out the existing branch. Record the starting
fork commit and inspect both sides before rebasing:

```bash
git rev-parse HEAD
git log --oneline --decorate HEAD..upstream/main
git log --oneline --decorate upstream/main..HEAD
```

The first log is the upstream work to integrate. The second is the macOS fork
work that must survive the rebase.

## 3. Rebase and adapt upstream changes

Start the rebase:

```bash
git rebase upstream/main
```

For every conflict:

1. Run `git status` and identify the upstream commit and files involved.
2. Read the complete conflicting files and the surrounding upstream diff.
3. Preserve macOS behavior and apply the compatible upstream behavior manually.
   Do not resolve a conflict by taking all of one side.
4. Check the project boundaries after each resolution:
   - platform code remains under `src/mac/`;
   - shared code remains plain C++23/Qt;
   - macOS capture, window management, OCR, hotkeys, notifications, login-item
     behavior, and test fakes remain intact;
   - the agent never gates startup on Screen Recording permission.
5. Stage only the resolved files and continue:

   ```bash
   git add <resolved-files>
   GIT_EDITOR=true git rebase --continue
   ```

If a conflict cannot be resolved confidently, run `git rebase --abort` on the
new sync branch, leave the original branch untouched, and report the commit,
files, and decision needed from the user.

After the rebase, inspect all resulting changes rather than only the conflict
resolution:

```bash
git diff --check
git diff --stat upstream/main...HEAD
git log --oneline --decorate upstream/main..HEAD
```

Use `rg` to check any changed build or source files for accidental
Wayland/Hyprland/X11 dependencies. Adapt the code and tests when upstream
behavior is useful but assumes Linux. Keep the changes narrowly focused on
making upstream functionality work on macOS.

## 4. Verify before publishing

Run the repository's complete verification command:

```bash
make check
```

This command must finish successfully, including the headless smoke suite and
available static analysis. If it fails:

1. Determine whether the failure is from the upstream change, a macOS
   adaptation, or the environment.
2. Fix code or tests only when the fix is clearly required for this sync.
3. Rerun `make check` from a clean build state as needed.

Do not weaken, skip, or delete a test to make the sync pass. If the failure is
environmental or requires a product decision, stop without publishing and
report the command, failure, and current branch.

Before pushing, require:

```bash
git status --short
git diff --check
```

The status must be clean. If conflict adaptations created new changes that
are not already part of the rebased commits, commit them with a focused
message such as:

```bash
git add <adapted-files>
git commit -m "Adapt upstream changes for macOS"
```

## 5. Push and open the pull request

Check GitHub authentication only after verification:

```bash
gh auth status
```

Check for an existing open PR from the sync branch. If none exists, publish
the new branch without force:

```bash
git push --set-upstream origin HEAD
gh pr create \
  --repo redox/fomosnap \
  --base main \
  --head "$(git branch --show-current)" \
  --title "Sync upstream omasnap into the macOS port" \
  --body "$(cat <<'EOF'
## Summary

- Rebase the macOS fork onto upstream `tobi/omasnap` `main` at `<upstream-sha>`.
- Adapt upstream shared functionality while preserving the native macOS platform layer.
- Keep the fork's macOS-specific fixes and tests intact.

## Test plan

- [x] `make check`
- [x] `git diff --check`

## Review notes

List any upstream commits that required non-trivial macOS adaptation and call
out behavior that was intentionally not ported.
EOF
)"
```

Replace placeholders with the actual upstream SHA and review notes. If an open
PR already exists for the branch, update its body with the current test result
instead of creating a duplicate. Return the PR URL, the upstream SHA, the
rebased commit range, and the exact verification result.
