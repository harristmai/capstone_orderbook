# How to Push to GitHub: capstone_orderbook

## Quick Steps

### 1. Configure Git (First Time Only)
```bash
git config --global user.name "Your Name"
git config --global user.email "your.email@example.com"
```

### 2. Commit Your Code
```bash
cd /home/chezboy/school_work/CAPSTONE/OME_OrderBook
git commit -m "Initial commit: High-performance orderbook with data fabric simulation"
```

### 3. Create GitHub Repository
1. Go to: https://github.com/new
2. **Repository name**: `capstone_orderbook`
3. **Description**: `High-performance order management engine with FPGA data fabric simulation`
4. **Choose**: Public or Private
5. **Important**: DO NOT check "Add a README file" (we already have one)
6. Click **"Create repository"**

### 4. Connect and Push
Replace `YOUR_GITHUB_USERNAME` with your actual username:

```bash
cd /home/chezboy/school_work/CAPSTONE/OME_OrderBook
git remote add origin https://github.com/YOUR_GITHUB_USERNAME/capstone_orderbook.git
git branch -M main
git push -u origin main
```

### 5. Verify
Go to: `https://github.com/YOUR_GITHUB_USERNAME/capstone_orderbook`

You should see all your files!

---

## What's Included

Files that will be pushed:
- ✅ `include/orderbook.h` - Header with all classes
- ✅ `src/orderbook.cpp` - Implementation
- ✅ `src/main.cpp` - Demo application
- ✅ `CMakeLists.txt` - Build configuration
- ✅ `README.md` - Project overview
- ✅ `.gitignore` - Ignore build files
- ✅ Documentation files (*.md)
- ✅ Benchmarks

Files that will be ignored (via .gitignore):
- ❌ `build/` directory
- ❌ Compiled binaries (*.o, *.exe, etc.)
- ❌ IDE files (.vscode, .idea, etc.)

---

## Troubleshooting

### "Permission denied" when pushing
You may need to authenticate. GitHub removed password authentication, so you need to:

**Option 1: Personal Access Token**
1. Go to: https://github.com/settings/tokens
2. Click "Generate new token (classic)"
3. Select scopes: `repo`
4. Copy the token
5. When prompted for password, use the token instead

**Option 2: SSH Key**
```bash
# Generate SSH key
ssh-keygen -t ed25519 -C "your.email@example.com"

# Copy public key
cat ~/.ssh/id_ed25519.pub

# Add to GitHub: https://github.com/settings/keys
```

Then use SSH URL instead:
```bash
git remote set-url origin git@github.com:YOUR_USERNAME/capstone_orderbook.git
```

### Already committed with wrong email?
```bash
git config user.email "correct.email@example.com"
git commit --amend --reset-author --no-edit
```

---

## Future Updates

After initial push, to update your repository:

```bash
# Make changes to your code...

# Stage changes
git add .

# Commit
git commit -m "Description of changes"

# Push
git push
```

---

## Clone on Another Machine

Once on GitHub, you or others can clone it:

```bash
git clone https://github.com/YOUR_USERNAME/capstone_orderbook.git
cd capstone_orderbook
mkdir build && cd build
cmake ..
make
./orderbook_main
```
