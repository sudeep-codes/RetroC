import os
import subprocess
import sys

def run_golden_tests():
    retroc_exe = sys.argv[1] if len(sys.argv) > 1 else "./build/retroc.exe"
    golden_dir = "tests/golden/edit"
    
    passed = 0
    total = 0
    
    files = sorted([f for f in os.listdir(golden_dir) if f.endswith(".rc")])
    for f in files:
        base = os.path.splitext(f)[0]
        rc_path = os.path.join(golden_dir, f)
        rcs_path = os.path.join(golden_dir, base + ".rcs")
        expected_path = os.path.join(golden_dir, base + ".expected")
        
        if not os.path.exists(rcs_path):
            print(f"FAILED: missing {rcs_path}")
            sys.exit(1)
        if not os.path.exists(expected_path):
            print(f"FAILED: missing {expected_path}")
            sys.exit(1)
            
        res = subprocess.run([retroc_exe, "edit", rc_path, rcs_path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        actual = (res.stdout if res.returncode == 0 else res.stderr).replace("\r\n", "\n")
            
        with open(expected_path, "r", encoding="utf-8") as exp_f:
            expected = exp_f.read().replace("\r\n", "\n")
            
        if actual.strip() != expected.strip():
            print(f"FAILED: mismatch in {f}")
            print("--- Expected ---")
            print(expected[:300])
            print("--- Actual ---")
            print(actual[:300])
            sys.exit(1)
            
        # Also run retroc verify
        res_ver = subprocess.run([retroc_exe, "verify", rc_path, rcs_path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res_ver.returncode != 0:
            print(f"FAILED: verify failed on {f}")
            print(res_ver.stderr)
            sys.exit(1)
            
        passed += 1
        total += 1
        
    print(f"All {passed}/{total} edit golden tests PASSED.")

if __name__ == "__main__":
    run_golden_tests()
