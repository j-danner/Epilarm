#file collection funcs for analyzing log files

#for comparison of time-stamps
from datetime import datetime, timedelta


#find 'gaps' in logs
def find_gaps(logs, timeframe_hours=24, gap_size_secs=1.5, gap_size_secs_max=-1):
    timeframe = datetime.today() - timedelta(hours=timeframe_hours)
    d = timedelta(seconds=gap_size_secs)
    if gap_size_secs_max==-1:
        d_max = timedelta.max
    else:
        d_max = timedelta(seconds=gap_size_secs_max)
    logs = [l for l in logs if l['time'] > timeframe]
    logs = sorted(logs, key=lambda k: k['time'])
    no_gaps = 0
    avg_gap_len = timedelta(seconds=0)
    for i in range(0,len(logs)-1):
      gap = logs[i+1]['time']-logs[i]['time']
      if gap > d and d_max > gap:
        #logs are more than d appart
        print("gap of " + str(gap) + " between " + str(logs[i]['time']) + " and " + str(logs[i+1]['time']))
        no_gaps = no_gaps + 1
        avg_gap_len = avg_gap_len + gap
    print("A total of " + str(no_gaps) + " 'gaps' of length between " + str(gap_size_secs) + " and " + str(gap_size_secs_max) + " seconds were detected in the logs of the last " + str(timeframe_hours) + " hours.")
    if no_gaps > 0:
        print ("The average length of a gap was " + str(avg_gap_len/no_gaps))


  
