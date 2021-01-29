#script for inspection of log files


import log_io
import log_analysis as analysis


# ftp credentials
mysite = "192.168.178.33"
username = "admin"
password = "T5tUZKVKWq8FPAh5"
port = "21"
# path to log files
remote_dir = "share/epilarm/log/jul/"
local_dir = "/home/julian/Epilarm/log_analysis/logs/"

# download + extract log files
log_dir = log_io.download_logs(mysite, username, password, port, remote_dir, local_dir)

log_dir = local_dir + remote_dir + "json_logs/"
# load log files
logs = log_io.load_logs(log_dir)


analysis.find_gaps(logs, timeframe_hours=12, gap_size_secs=0.0, gap_size_secs_max=0.9)
analysis.find_gaps(logs, timeframe_hours=12, gap_size_secs=1.1, gap_size_secs_max=1.9)
analysis.find_gaps(logs, timeframe_hours=12, gap_size_secs=1.5, gap_size_secs_max=-1)
# find_gaps(logs, timeframe_hours=12, gap_size_secs=0.9, gap_size_secs_max=1.1)




#remove logs older than 1 day
#today = datetime.combine(datetime.today(), datetime.min.time())
#logs_today = [l for l in logs if l['time']>today]
#print(logs_today[0]['time'])




