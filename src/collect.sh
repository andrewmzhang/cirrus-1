
curr_date=$(date +%Y%m%d_%H%M%S)
echo "$curr_date"
mkdir "logs/$curr_date"

for var in "$@"
do
  echo "$var"
  scp $var:~/cirrus-1/src/logs/log.txt logs/$curr_date/$var.txt
  awk '{print $(NF-1), $NF}' logs/$curr_date/$var.txt >> logs/$curr_date/agg.txt
done

python sum.py logs/$curr_date/agg.txt

