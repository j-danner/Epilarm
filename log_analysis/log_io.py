# funcs for downloading and processing of log files

### to download tar.gz's of log files ###
import ftplib
import ftp

### to extract log files ###
import tarfile
import os

### to read json files ###
import json
from datetime import datetime, timedelta


# download and extract log-files from server
def download_logs(mysite, username, password, port, remote_dir, local_dir):
    """ downloads all files from ftp server and extracts all tar.gz files """
    print("downloading tar.gz-files...")
    #url = 'ftp://'+username+":"+password+"@"+mysite+":"+port+"/"+remote_dir+"/"
    ftp_server = ftplib.FTP(mysite, username, password)
    ftp.download_ftp_tree(ftp_server, remote_dir, local_dir, overwrite=False, guess_by_extension=True)
    print("extracting files, this may take a while...")
    dir = local_dir + remote_dir
    log_dir = dir + "json_logs/"
    for filename in os.listdir(dir):
        if filename.endswith(".tar.gz"):
            tar = tarfile.open(dir+filename, "r:gz")
            tar.extractall(path=dir+"/json_logs")
            tar.close()
    # json-log-files can now be found in log_dir
    print("done!")
    return log_dir


def load_logs(log_dir):
    """ load all logs and convert their time-stamps to datetime format """
    # local func used for parsing...
    def parse_nan_inf(arg):
        print("got:",arg)
        c = {"-Infinity":-float("inf"), "Infinity":float("inf"), "NaN":float("nan")}
        return c[arg]
    # load log files...
    logs = []
    for filename in os.listdir(log_dir):
        if filename.endswith(".json"):
            f = open(log_dir + filename)
            try:
              logs.append(json.load(f, parse_constant=parse_nan_inf))
            except ValueError:
              print(filename + " could not be loaded; probably nan or inf values contained!")
            continue
        else:
          continue
    # convert timestamp string to datetime object
    for i in range(0,len(logs)):
        logs[i].update({'time': datetime.strptime(logs[i][u'time'], '%Y-%m-%dT%H:%M:%S.%f')})
    #sort by timestamp
    logs = sorted(logs, key=lambda k: k['time']) 
    return logs



