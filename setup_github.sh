#!/bin/bash
# Script to push OME_OrderBook to GitHub

echo "=== Setting up GitHub Repository: capstone_orderbook ==="
echo ""

# Step 1: Configure git if needed
echo "Step 1: Configure git identity (if not already set)"
echo "Run these commands with YOUR information:"
echo "  git config --global user.name \"Your Name\""
echo "  git config --global user.email \"your.email@example.com\""
echo ""
read -p "Press Enter when you've configured git (or if already configured)..."

# Step 2: Commit changes
echo ""
echo "Step 2: Creating initial commit..."
git commit -m "Initial commit: High-performance orderbook with data fabric simulation"

# Step 3: Go to GitHub and create repository
echo ""
echo "Step 3: Create GitHub repository"
echo "  1. Go to: https://github.com/new"
echo "  2. Repository name: capstone_orderbook"
echo "  3. Description (optional): High-performance order management engine with FPGA data fabric simulation"
echo "  4. Choose: Public or Private"
echo "  5. DO NOT initialize with README (we already have one)"
echo "  6. Click 'Create repository'"
echo ""
read -p "Press Enter when you've created the repository on GitHub..."

# Step 4: Get your GitHub username
echo ""
read -p "Enter your GitHub username: " GITHUB_USER

# Step 5: Add remote and push
echo ""
echo "Step 5: Connecting to GitHub and pushing..."
git remote add origin "https://github.com/${GITHUB_USER}/capstone_orderbook.git"
git branch -M main
git push -u origin main

echo ""
echo "=== Done! ==="
echo "Your repository should now be live at:"
echo "https://github.com/${GITHUB_USER}/capstone_orderbook"
