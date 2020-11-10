import ftplib
import ftp

### download log files ###
mysite = "192.168.178.33"
username = "admin"
password = "T5tUZKVKWq8FPAh5"
remote_dir = "share/epilarm/log/"
local_dir = "/home/julian/Epilarm/log_analysis/logs/"
ftp_server = ftplib.FTP(mysite, username, password)
ftp.download_ftp_tree(ftp_server, remote_dir, local_dir, overwrite=False, guess_by_extension=True)


### read json files ###
import os
import json


def parse_nan_inf(arg):
    print("got:",arg)
    c = {"-Infinity":-float("inf"), "Infinity":float("inf"), "NaN":float("nan")}
    return c[arg]


dir = local_dir + remote_dir
logs = []

for filename in os.listdir(dir):
    if filename.endswith(".json"):
        f = open(dir + filename)
        try:
          logs.append(json.load(f, parse_constant=parse_nan_inf))
        except ValueError:
          print(filename + " could not be loaded; probably nan of inf values contained!")
        continue
    else:
      continue

# logs are now loaded as dicts in logs

#replace timestamp-strings by timestamp-datetime
from datetime import datetime, timedelta

for i in range(0,len(logs)):
  logs[i].update({'time': datetime.strptime(logs[i][u'time'], '%Y-%m-%dT%H:%M:%S.%f')})

#sort by timestamp
logs = sorted(logs, key=lambda k: k['time']) 


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
    print ("The average length of a gap was " + str(avg_gap_len/no_gaps))


find_gaps(logs, timeframe_hours=12, gap_size_secs=1.5, gap_size_secs_max=600)


#remove logs older than 1 day
today = datetime.combine(datetime.today(), datetime.min.time())
logs_today = [l for l in logs if l['time']>today]

print(logs_today[0]['time'])




