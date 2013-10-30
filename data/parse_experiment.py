import sys
import os

def usage():
    print >> sys.stderr, "usage: python parse_experiment.py list_of_program_prefixes"

def main():
    print >> sys.stderr, "Starting to parse output..."
    dataBucket = {} # map to list of lists
    fname = sys.argv[1]
    prefix_fnames = open(fname, 'r')
    for prefix in prefix_fnames:
        prefix = prefix.strip()
        print >> sys.stderr, "Parsing: " + str(prefix)

        # Add this prefix to the data bucket
        dataBucket[prefix] = []
        dataBucket[prefix].append([]) # list for modified data time
        dataBucket[prefix].append([]) # lost for unmodified data time
        for dirpath, dnames, fnames in os.walk("."):
            for f in fnames:
                if f.startswith(prefix):
                    full = os.path.join(dirpath, f)

                    # Extract time
                    ftimef = open(full, 'r')
                    size = 0
                    time = 0
                    for l in ftimef:
                        if "TIME:" in l:
                            data = l.split(":")[1].split(",")
                            size = data[0]
                            time = float(data[1])
                            break
                    if ".mod." in f:
                        dataBucket[prefix][0].append((size, time))
                    else:
                        dataBucket[prefix][1].append((size, time))

        # Generate CSV output 
        minSamples = min(len(dataBucket[prefix][0]), len(dataBucket[prefix][1]))
        csv = ""
        for i in range(len(dataBucket[prefix][0])):
            for j in range(len(dataBucket[prefix][1])):
                if (dataBucket[prefix][0][i][0] == dataBucket[prefix][1][j][0]): # if these times correspond to the same input size...
                    size = dataBucket[prefix][0][i][0]
                    time1 = dataBucket[prefix][0][i][1]
                    time2 = dataBucket[prefix][1][j][1]
                    csv = csv + str(size) + "," + str(time1) + "," + str(time2) + "\n"
        print(prefix + " CSV")
        print(csv)

if __name__ == "__main__":
    main()
