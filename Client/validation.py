import time
import uuid
import random
import sys
from client import RedBoxClient

# ==========================================
# TEST FRAMEWORK
# ==========================================
class TestSuite:
    def __init__(self):
        self.passed = 0
        self.failed = 0

    def log(self, msg):
        print(f"   [INFO] {msg}")

    def assert_true(self, condition, test_name):
        if condition:
            print(f"   [PASS] {test_name}")
            self.passed += 1
        else:
            print(f"   [FAIL] {test_name}")
            self.failed += 1
    
    def assert_equal(self, actual, expected, test_name):
        if actual == expected:
            print(f"   [PASS] {test_name} (Got {actual})")
            self.passed += 1
        else:
            print(f"   [FAIL] {test_name} (Expected {expected}, Got {actual})")
            self.failed += 1

    def generate_id(self):
        return random.randint(1000, 999999)

    def summary(self):
        print("\n" + "="*40)
        print(f"   TEST SUMMARY: {self.passed} Passed, {self.failed} Failed")
        print("="*40)

# ==========================================
# ACTUAL TESTS
# ==========================================
def run_validation():
    tester = TestSuite()
    print("==========================================")
    print("   RedBoxDb COMPREHENSIVE VERIFICATION    ")
    print("==========================================")

    # --- TEST 1: CORE MATH & LOGIC ---
    print("\n[TEST 1] Vector Math & Proximity Search")
    db_name = "verify_math"
    tester.log(f"Connecting to DB: {db_name} (Dim=3)")
    
    with RedBoxClient(db_name=db_name, dim=3) as client:
        vec_x = [1.0, 0.0, 0.0] # ID 10
        vec_y = [0.0, 1.0, 0.0] # ID 20
        vec_z = [0.0, 0.0, 1.0] # ID 30
        
        client.insert(10, vec_x)
        client.insert(20, vec_y)
        client.insert(30, vec_z)
        tester.log("Inserted 3 orthogonal unit vectors.")

        # Query 1: Close to X
        res = client.search([0.9, 0.1, 0.0])
        tester.assert_equal(res, 10, "Search proximity to X-Axis")

        # Query 2: Close to Y
        res = client.search([0.1, 0.9, 0.0])
        tester.assert_equal(res, 20, "Search proximity to Y-Axis")

    # --- TEST 2: CRUD LIFECYCLE ---
    print("\n[TEST 2] CRUD Lifecycle (Create-Read-Update-Delete)")
    
    # Use UUID to prevent zombie data failures
    db_name = f"verify_crud_{uuid.uuid4().hex[:8]}"
    target_id = tester.generate_id()
    
    with RedBoxClient(db_name=db_name, dim=4) as client:
        # CREATE
        data = [1, 1, 1, 1]
        client.insert(target_id, data)
        tester.assert_equal(client.search(data), target_id, "Insert & Retrieve")

        # UPDATE (In-Place)
        new_data = [2, 2, 2, 2]
        success = client.update(target_id, new_data)
        tester.assert_true(success, "Update command returned Success")
        
        # VERIFY UPDATE
        found_id = client.search(new_data)
        tester.assert_equal(found_id, target_id, "Search found Updated Vector")
        
        # DELETE
        client.delete(target_id)
        tester.log(f"Deleted ID {target_id}")

        # VERIFY DELETE
        distractor_id = target_id + 1
        client.insert(distractor_id, [5, 5, 5, 5])
        
        found_id = client.search(new_data)
        tester.assert_true(found_id != target_id, "Deleted ID was NOT found")
        tester.assert_equal(found_id, distractor_id, "Search fell back to Distractor")

    # --- TEST 3: ISOLATION & VALIDATION ---
    print("\n[TEST 3] Multi-Tenant Isolation & Error Handling")
    
    with RedBoxClient(db_name="verify_iso_tiny", dim=2) as db_small:
        db_small.insert(1, [1, 1])
    
    with RedBoxClient(db_name="verify_iso_huge", dim=10) as db_large:
        # 1. Check isolation
        try:
            db_large.search([1, 1])
            tester.assert_true(False, "Failed to block dimension mismatch")
        except ValueError as e:
            tester.assert_true(True, f"Blocked Dimension Mismatch: {e}")

        # 2. Check strict update on non-existent ID
        success = db_large.update(99999, [0]*10)
        tester.assert_true(not success, "Strict Update returned False for missing ID")

    # --- TEST 4: DATA PERSISTENCE ---
    print("\n[TEST 4] Persistence (Simulated Restart)")
    db_name = "verify_persist"
    persist_id = 777
    persist_vec = [0.5, 0.5, 0.5]

    # Phase 1: Write and Disconnect
    tester.log("Phase 1: Writing data and closing connection...")
    with RedBoxClient(db_name=db_name, dim=3) as client:
        client.insert(persist_id, persist_vec)
    
    time.sleep(0.5) 

    # Phase 2: Reconnect
    tester.log("Phase 2: Opening new connection to same DB...")
    with RedBoxClient(db_name=db_name, dim=3) as client:
        res = client.search(persist_vec)
        tester.assert_equal(res, persist_id, "Data survived reconnection")

    tester.summary()
    
    if tester.failed > 0:
        sys.exit(1) # Ensure CI knows it failed

if __name__ == "__main__":
    try:
        run_validation()
    except Exception as e:
        print(f"\n[CRITICAL ERROR] {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)