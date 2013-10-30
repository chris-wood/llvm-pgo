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
        if len(prefix) > 0:
            print >> sys.stderr, "Parsing: " + str(prefix)

            # Add this prefix to the data bucket
            dataBucket[prefix] = []
            dataBucket[prefix].append([]) # list for modified data time
            dataBucket[prefix].append([]) # list for unmodified data time

            dataBucket[prefix].append([]) # first modified
            dataBucket[prefix].append([]) # first unmodified

            dataBucket[prefix].append([]) # last modified
            dataBucket[prefix].append([]) # last unmodified
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
                                size = int(data[0])
                                time = float(data[1])
                                break
                        if ".first." and ".mod" in f:
                            dataBucket[prefix][2].append((size, time))
                        elif ".first." in f:
                            dataBucket[prefix][3].append((size, time))
                        elif ".last." and ".mod" in f:
                            dataBucket[prefix][4].append((size, time))
                        elif ".last." in f:
                            dataBucket[prefix][5].append((size, time))
                        elif ".mod." in f:
                            dataBucket[prefix][0].append((size, time))
                        else:
                            dataBucket[prefix][1].append((size, time))

            # Generate CSV output for same input comparison
            minSamples = min(len(dataBucket[prefix][0]), len(dataBucket[prefix][1]))
            csv = "size,optimized,unoptimized\n"
            for i in range(len(dataBucket[prefix][0])):
                for j in range(len(dataBucket[prefix][1])):
                    if (dataBucket[prefix][0][i][0] == dataBucket[prefix][1][j][0]): # if these times correspond to the same input size...
                        size = dataBucket[prefix][0][i][0]
                        time1 = dataBucket[prefix][0][i][1]
                        time2 = dataBucket[prefix][1][j][1]
                        csv = csv + str(size) + "," + str(time1) + "," + str(time2) + "\n"
            
            # Write the data to a CSV file
            csvf = open(prefix + ".data.csv", 'w')
            csvf.write(csv + "\n")
            csvf.close()

            ###### FIRST

            # Generate CSV output for first comparison
            csv = "size,optimized,unoptimized"
            for i in range(len(dataBucket[prefix][2])):
                for j in range(len(dataBucket[prefix][3])):
                    if (dataBucket[prefix][2][i][0] == dataBucket[prefix][3][j][0]): # if these times correspond to the same input size...
                        size = dataBucket[prefix][2][i][0]
                        time1 = dataBucket[prefix][2][i][1]
                        time2 = dataBucket[prefix][3][j][1]
                        csv = csv + str(size) + "," + str(time1) + "," + str(time2) + "\n"
            
            # Write the data to a CSV file
            csvf = open(prefix + ".first.data.csv", 'w')
            csvf.write(csv + "\n")
            csvf.close()

            ###### LAST

            # Generate CSV output for first comparison
            csv = "size,optimized,unoptimized"
            for i in range(len(dataBucket[prefix][4])):
                for j in range(len(dataBucket[prefix][5])):
                    if (dataBucket[prefix][4][i][0] == dataBucket[prefix][5][j][0]): # if these times correspond to the same input size...
                        size = dataBucket[prefix][4][i][0]
                        time1 = dataBucket[prefix][4][i][1]
                        time2 = dataBucket[prefix][5][j][1]
                        csv = csv + str(size) + "," + str(time1) + "," + str(time2) + "\n"
            
            # Write the data to a CSV file
            csvf = open(prefix + ".last.data.csv", 'w')
            csvf.write(csv + "\n")
            csvf.close()

if __name__ == "__main__":
    main()
