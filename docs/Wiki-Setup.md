# GitHub Wiki Setup Instructions

The development documentation has been prepared and is ready to be added to GitHub Wiki.

## Quick Setup (Recommended)

1. **Enable Wiki** (already enabled for this repo)

2. **Create Wiki pages via GitHub Web UI:**
   - Go to: https://github.com/m1ngsama/TNT/wiki
   - Click "Create the first page"
   - Title: `Home`
   - Content: Copy from `README.md` (project overview)
   - Click "Save Page"

3. **Add Development Guide:**
   - Click "New Page"
   - Title: `Development-Guide`
   - Content: Copy from `docs/Development-Guide.md`
   - Click "Save Page"

4. **Add other pages as needed:**
   - Contributing Guide (from `docs/CONTRIBUTING.md`)
   - Deployment Guide (from `docs/DEPLOYMENT.md`)
   - Security Reference (from `docs/SECURITY_QUICKREF.md`)
   - Quick Reference (from `docs/QUICKREF.md`)

## Alternative: Clone and Push

Once the Wiki has at least one page, you can manage it via git:

```sh
# Clone wiki repository
git clone https://github.com/m1ngsama/TNT.wiki.git
cd TNT.wiki

# Copy documentation
cp ../docs/Development-Guide.md ./Development-Guide.md
cp ../docs/CONTRIBUTING.md ./Contributing.md
cp ../docs/DEPLOYMENT.md ./Deployment.md

# Commit and push
git add .
git commit -m "docs: add development documentation"
git push origin master
```

## Wiki Page Structure

Recommended structure:

```
Home                    - Project overview (from README.md)
├── Development-Guide   - Complete dev manual
├── Contributing        - How to contribute
├── Deployment          - Production deployment
├── Security-Reference  - Security configuration
└── Quick-Reference     - Command cheat sheet
```

## Notes

- GitHub Wiki uses its own git repository (separate from main repo)
- Wiki pages are written in Markdown
- Page titles become URLs (spaces → hyphens)
- First page must be created via Web UI
- After that, you can use git to manage content

---

**All documentation is already in `docs/` directory and can be viewed directly in the repository.**
