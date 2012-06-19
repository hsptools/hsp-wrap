#!/usr/bin/env python

import sys,re
from pprint import pprint
import matplotlib.pyplot as plt
from matplotlib.transforms import Bbox

worker_re = re.compile(r'\[\d*\] \[TIMING\] Slave (\d*), Worker (\d*) (.*) at (\S*).')

class Event:
    REQUESTING = 1
    BEGINNING  = 2
    DONE       = 3

str_to_event = {
    'Requesting': Event.REQUESTING,
    'beginning search': Event.BEGINNING,
    'done with search': Event.DONE,
    'inflating': None
}

times = {}
t_max = 0

# Find or create timeline for this worker
def find_time(wid):
    try:
        t = times[worker]
    except KeyError:
        t = times[worker] = []
    return t

# Format an array of times (with possibly incomplete records) to a format for plt
def xranges(l):
    for i in l:
        if type(i) == tuple:
            yield i
        else:
            yield (i, t_max - i)

# Format an array of times into color values
def colors(l):
    for i in l:
        if type(i) == tuple:
            yield 'cyan'
        else:
            yield 'red'

# Fill 'times' structure
with open(sys.argv[1]) as f:
    for line in f:
        m = worker_re.search(line)
        # Line is a timing line we are interested in
        if m:
            # So fetch the data
            worker = (int(m.group(1)), int(m.group(2)))
            evnt   = str_to_event[m.group(3)]
            t1     = float(m.group(4))
            t_max  = max(t_max, t1)

            if evnt == Event.BEGINNING:
                t = find_time(worker)
                t.append(t1)

            elif evnt == Event.DONE:
                t   = find_time(worker)
                t0  = t.pop()
                dur = t1 - t0
                t.append((t0, dur))

times_list = sorted(times.items(), key = lambda x: x[0], reverse = True)

# Plot it
fig = plt.figure()
ax = fig.add_subplot(111)

y = 0
for i in times_list:
    ax.broken_barh(xranges(i[1]), (y, 9), facecolors=[c for c in colors(i[1])])
    y += 10

#ax.set_ylim(0,100)
#ax.set_xlim(8,24)
ax.set_xlabel('seconds since start')

ax.set_yticks(xrange(5, len(times)*10, 10))
ax.set_yticklabels(['(%d, %d)' % i[0] for i in times_list])
ax.grid(True)

#plt.savefig('timeline.eps')
plt.show()
