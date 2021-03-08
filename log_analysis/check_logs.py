# funcs to check if 'extended' logs are correct
# i.e. verify that comp of c-code is correct, i.e., avgroi and avgnroi are
# correctly calculated from x|y|z_spec.

import math

checklogs(logs)
log=_
[i for i in range(0,len(logs)) if logs[i]==log][0]

x_spec = log['x_spec']
y_spec = log['y_spec']
z_spec = log['z_spec']

avgroi_, avgnroi_ = avgs([x_spec, y_spec, z_spec], minFreq, maxFreq)

avgroi_==log['avg_roi']

avgnroi_
log['avg_nroi']
#these are not equal since we only have access to the first 20 bins of simplified specs, but nroi is computed with all 25 bins :/

multRatio(log['avg_roi'], log['avg_nroi']) - log['multRatio'] < 10**(-16)

avgRoi(log['avg_roi']) - log['avgRoi'] < 10**(-16)




def checklogs(logs):
    for log in logs:
        avg_roi = log['avg_roi']
        avg_nroi = log['avg_nroi']
        minFreq = log['params'][0]['minFreq']
        maxFreq = log['params'][0]['maxFreq']
        avgroi_, avgnroi_ = avgs([log['x_spec'], log['y_spec'], log['z_spec']], minFreq, maxFreq)
        if avgroi_!=avg_roi:
            print 'avgroi incorrectly computed from spec!'
            return log
        #avgnroi_==avg_nroi ##not yet guaranteed!!
        if abs(log['multRatio'] - multRatio(avg_roi, avg_nroi)) > 10**(-15):
            print 'multRatio incorrectly computed from avg_roi and avg_nroi!'
            return log
        if abs(log['avgRoi'] - avgRoi(avg_roi)) > 10**(-15):
            print 'avgRoi incorrectly computed from avg_roi!'
            return log
    print("done!")


def multRatio(avg_roi, avg_nroi):
    return sum([a/b for a,b in zip(avg_roi, avg_nroi)])/3.0


def avgRoi(avg_roi):
    return math.sqrt(sum(avg**2 for avg in avg_roi))


def avgroi_nroi_(spec, minFreq, maxFreq):
    avg_roi = sum(spec[i] for i in range(int(2*minFreq), int(2*maxFreq)+1))
    avg_nroi = sum(spec) - avg_roi
    #normalize avgs
    avg_nroi = (avg_nroi-avg_roi) / (len(spec)-(2*maxFreq-2*minFreq+1))
    avg_roi = avg_roi / (2*maxFreq-2*minFreq+1)
    return [avg_roi, avg_nroi]

def avgs(specs, minFreq, maxFreq):
    avgroi = []
    avgnroi = []
    for spec in specs:
        avgroi_, avgnroi_ = avgroi_nroi_(spec, minFreq, maxFreq)
        avgroi += [avgroi_]
        avgnroi += [avgnroi_]
    return avgroi, avgnroi


