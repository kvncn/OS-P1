-_________FAILED TESTCASE REPORT_________-

### Testcase 03 (FAILED BUT SHOULD PASS)
-----------------------------------------
- Difference in architectures and implementation causes the clockHandler() to fire off once in some runs. Also, it's highly possible that our implementation is slower than Russ's. This leads to us facing a clock interrupt.

### Testcase 13 (FAILED, SHOULD PASS)
-----------------------------------------
- This testcase should pass, it fails because of a tie breaker difference in race conditons in our scheduler, which causes testcase main to run before XXp3(), which makes it get blocked inside join(), and thus we see that difference. 

### Testcase 14 (FAILED, SHOULD PASS)
-----------------------------------------
- This testcase should pass, it fails because of a tie breaker difference in race conditons in our scheduler, which causes testcase main to run before XXp3(), which makes it get blocked inside join(), and thus we see that difference. 

### Testcase 15 (FAILED, SHOULD PASS)
-----------------------------------------
- This testcase should pass, it fails because of a tie breaker difference in race conditons in our scheduler, which causes testcase main to run before XXp3(), which makes it get blocked inside join(), and thus we see that difference. 

### Testcase 16 (FAILED, SHOULD PASS)
-----------------------------------------
- This testcase should pass, it fails because of a tie breaker difference in race conditons in our scheduler, which causes testcase main to run before XXp3(), which makes it get blocked inside join(), and thus we see that difference. 

### Testcase 18 (FAILED, SHOULD PASS)
-----------------------------------------
- This testcase should pass, it fails because of a tie breaker difference in race conditons in our scheduler, which causes testcase main to run before XXp3(), which makes it get blocked inside join(), and thus we see that difference. 

- This testcase should pass, it fails because of a tie breaker difference in race conditons in our scheduler, which causes testcase main to be called before the end of XXp3()'s while loop.
- Due to this sceduling difference, the spin loop in the code for XXp3 terminates instantly, leaving us with no reason to call the clockHandler() repeatedly like Russ' code does.
- Because our scheduling works in a way that the spin loop in XXp3() never gets a chance to run, thereby we never end up hogging CPU resources
- Finally, the implementation of our join collects the statuses of the dead children in the order in they were created, not the order in which they quit(). The spec doesn't specify which child to collect first, so this shouldn't cause errors

### Testcase 20 (FAILED, SHOULD PASS)
-----------------------------------------
- This testcase should pass, it fails because of a tie breaker difference in race conditons in our scheduler, which causes testcase main to run before XXp3(), which makes it get blocked inside join(), and thus we see that difference.


### Testcase 27 (FAILED, SHOULD FAIL)
-----------------------------------------

SUMMARY
-----------------------------------------
- Our decision-making is implemented differently, which is why in race conditions, the process that was created first will execute first which is not the same as Russ' implementation
- Race issue due to clockHandler() for test03