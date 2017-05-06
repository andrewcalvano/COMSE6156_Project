import re
import sys

fh = open("output", "r")

contents = fh.read()

measurements = re.findall(r'(\d+)\% ' + sys.argv[1], contents)

measurements = map(lambda x: float(x), measurements)

measurements = filter(lambda x: x != 0, measurements)

print sum(measurements) / len(measurements)

fh.close()
