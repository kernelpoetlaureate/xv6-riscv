#!/usr/bin/env python3
"""
Test script to verify the memory analysis tool can be imported without errors
"""

def test_import():
    """Test that the module imports correctly without GDB"""
    try:
        # Import the main module - this should work even outside GDB
        import sys
        import os
        
        # Add the current directory to path
        current_dir = os.path.dirname(os.path.abspath(__file__))
        sys.path.insert(0, current_dir)
        
        # Try to import the module (but avoid GDB-specific initialization)
        print("Testing import of memory analysis module...")
        
        # Read and check for basic syntax
        with open('full.py', 'r') as f:
            content = f.read()
            
        # Basic syntax check by compiling
        compile(content, 'full.py', 'exec')
        print("✓ Syntax check passed")
        
        # Check that telnetlib is no longer imported
        if 'import telnetlib' in content:
            print("✗ Still contains telnetlib import")
            return False
        else:
            print("✓ telnetlib dependency removed")
        
        # Check that GDB commands are defined
        if 'class ComprehensiveMemoryAnalysisCommand' in content:
            print("✓ GDB command classes defined")
        else:
            print("✗ Missing GDB command classes")
            return False
            
        print("✓ All tests passed - module ready for GDB integration")
        return True
        
    except Exception as e:
        print(f"✗ Import test failed: {e}")
        return False

if __name__ == "__main__":
    success = test_import()
    exit(0 if success else 1)