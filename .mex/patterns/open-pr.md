---
name: open-pr
description: Open a pull request with the mandatory pre-PR Ollama code review. Use whenever opening a PR from dev to main or a feature branch to dev.
triggers:
  - "open pr"
  - "create pull request"
  - "merge to main"
  - "code review"
edges:
  - target: context/conventions.md
    condition: when verifying coding-standard compliance before review
  - target: patterns/pio-build-test.md
    condition: when running native tests as part of pre-PR verify
last_updated: 2026-04-26
---

# Open a Pull Request

## Context

All PRs require an Ollama qwen3-coder code review before they can be merged. This is non-negotiable per `AGENTS.md`. The review runs on the dedicated Ollama server at `192.168.1.16` against the branch you want to merge.

Branch workflow:
- `main` = production (what's deployed)
- `dev` = integration branch
- Feature branches are off `dev`
- PR direction: feature → `dev`, then milestone PR `dev` → `main`
- "prod" in user vocabulary means `main`

## Steps

1. **Verify locally first.** Build silent (`pio run -s 2>&1 | tail -5`) and run native tests (`pio test -e native 2>&1 | grep -E "(PASS|FAIL|Tests|Ignored)"`). Both must be clean.
2. **Push the branch:** `git push -u origin <branch>`. The Ollama server pulls from origin, so the branch must be pushed before review.
3. **Run the Ollama review:**

   ```bash
   ssh natas@192.168.1.16 \
     "cd ~/repos/hivesense-monitor && git fetch origin && git checkout <branch> && \
      ~/code-review/review.sh ~/repos/hivesense-monitor <base-branch> qwen3-coder:30b"
   ```

   Where `<base-branch>` is `dev` (for feature → dev PRs) or `main` (for dev → main PRs).

4. **Address review findings.** The review writes a markdown report. Real findings get fixed in new commits on the same branch (NOT amended into existing commits). Push and re-review if changes are substantive.
5. **Create the PR:** `gh pr create --base <base-branch> --title "<title>" --body "<body>"`. Title under 70 chars; body has Summary + Test plan sections.
6. **Wait for GitHub Actions checks.** The CI runs builds for all four firmware envs + native tests. All must pass.
7. **Squash-merge** when ready: `gh pr merge <PR#> --squash`. We squash to keep `main`'s history flat.

## Gotchas

- **The Ollama server has the repo at `~/repos/hivesense-monitor`**, not `~/repos/combsense-monitor`. The CLAUDE.md note about the legacy name still applies.
- **CodeRabbit doesn't run on already-merged PRs.** If you trigger `@coderabbitai full review` on a closed PR, it acknowledges but skips silently. Get the review in BEFORE merging.
- **Don't include AI attribution** in commit messages or PR bodies. No `Co-Authored-By: Claude` lines, no "Generated with Claude" footers — see the user's hard rule.
- **Don't `--amend` after a hook failure.** A failed pre-commit hook means the commit didn't happen. Amend would modify the previous commit (potentially destroying work). Fix, re-stage, create a new commit.
- **Don't skip CI checks** with `--no-verify` or merge a red PR. The 12 GitHub Actions checks are the safety net.

## Verify

- [ ] Native tests green locally (`pio test -e native`)
- [ ] Build green for the affected firmware env(s)
- [ ] Ollama review report read and findings addressed (or explicitly waived with reason in the PR body)
- [ ] All GitHub Actions checks green before merge
- [ ] Branch direction correct (feature → dev, OR dev → main)
- [ ] No `Co-Authored-By` or AI attribution in commit / PR messages

## Debug

- **Ollama review hangs:** server is at `192.168.1.16:11434`. Check it's up: `curl -s http://192.168.1.16:11434/api/tags | head`.
- **`review.sh` not found on Ollama server:** the path is `~/code-review/review.sh`. If missing, verify SSH user is `natas` and that the review tooling has been installed there.
- **CI fails on a build for an env you didn't touch:** check whether shared/header changes affected it (`firmware/shared/`).

## Update Scaffold

- [ ] Update `.mex/ROUTER.md` "Current Project State" if the merged work changed deployment topology or added a new module
- [ ] Update `context/decisions.md` if the PR encoded a new architectural decision
- [ ] If the PR added a new task type, create a pattern for it
