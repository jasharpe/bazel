import matplotlib as mpl
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.cbook as cbook
import matplotlib.ticker as ticker
import sys

def read_datafile(file_name):
    # the skiprows keyword is for heading, but I don't know if trailing lines
    # can be specified
    data = np.loadtxt(file_name, delimiter=',')
    return data

data = np.genfromtxt(sys.argv[1], delimiter=',', names=['x', 'y'])

fig = plt.figure()

ax1 = fig.add_subplot(111)
ax1.plot(data['x'], data['y'], color='r', label='the data')
def format_nanos(x, pos=None):
  seconds=(x/1e9)%60
  seconds = int(seconds)
  minutes=(x/(1e9*60))%60
  minutes = int(minutes)
  hours=(x/(1e9*60*60))%24

  return ("%02d:%02d:%02d" % (hours, minutes, seconds))
ax1.xaxis.set_major_formatter(ticker.FuncFormatter(format_nanos))

plt.show()
