cp ../bin/msys2/Release/deallab.exe .
.\deallab -i testinput.txt -p test1 4 1S
echo Test 1: >testresults.txt
echo ======= >>testresults.txt
cat test1_north.txt >>testresults.txt
.\deallab -i testinput.txt -p test2 4 1S 1S1N
echo Test 2: >>testresults.txt
echo ======= >>testresults.txt
cat test2_north.txt >>testresults.txt
.\deallab -i testinput.txt -p test4 4 1S Any 1S1N Any
echo Test 4: >>testresults.txt
echo ======= >>testresults.txt
cat test4_north.txt >>testresults.txt
.\deallab -i testinput.txt -p test5 4 AK732.Q86.95.A63 Any 1S1N Any
echo Test 5: >>testresults.txt
echo ======= >>testresults.txt
cat test5_north.txt >>testresults.txt
.\deallab -i testinput.txt -p test6 4 AK732.Q86.95.A63 Any Q54.T753.AJ6.Q84 Any
echo Test 6: >>testresults.txt
echo ======= >>testresults.txt
cat test6_north.txt >>testresults.txt
.\deallab -i testinput.txt -h testhands.csv -r results -p testdetails 16
echo Test A: >>testresults.txt
echo ======= >>testresults.txt
cat testdetails.csv >>testresults.txt
diff testresults.txt baseline.txt
