#!/usr/bin/python

import sys
import getopt

def Usage():
  print "Unkown command line args"
  
  
def main():
  try:
    opts, args = getopt.getopt(sys.argv[1:],"hav:",["help","arch=","verbose"]) 
  except getopt.GetoptError:
    print "Unkown command line args"
    sys.exit(2)

  Verbose = 0
  for o,a in opts:
    if o in ("-h", "--help"):
      Usage ()
      sys.exit ()
    elif o in ("-a", "--arch"):
      Arch = a
    else:
      Usage ()
      sys.exit ()
    

  if Verbose:
	print "\nmach-o load commands:"
  otoolload = open("otool-load.log", "r")
  data = otoolload.read()
  otoolload.close()
  
  
  # extract extry point from '	    ss  0x00000000 eflags 0x00000000 eip 0x00000259 cs  0x00000000'
  if Arch == "i386":
    eip = data.find("eip")
    if eip != -1:
      EntryPoint = int (data[eip + 4:eip + 4 + 10], 16)
  
  if Arch == "arm":
    r15 = data.find("r15")
    if r15 != -1:
      EntryPoint = int (data[r15 + 4:r15 + 4 + 10], 16)
  
  # extract entry point from '   r15  0x0000000000000000 rip 0x0000000000000253'
  if Arch == "x86_64":
    rip = data.find("rip")
    if rip != -1:
      EntryPoint = int (data[rip + 4:rip + 4 + 18], 16)
  
  if EntryPoint == 0:
    print "FAIL - no entry point for PE/COFF image"
    sys.exit(-1)
  else:
	if Verbose:
		print "Entry Point = 0x%08x" % EntryPoint
  
  
  if Verbose:
	print "\nPE/COFF dump:"
  objdump = open("objdump-raw.log", "r")
  data = objdump.read()
  objdump.close()
  
  # Extract 'SizeOfImage		00000360'
  Index = data.find("SizeOfImage")
  End = data[Index:].find("\n")
  SizeOfImage = int (data[Index+11:Index + End], 16);
  if Verbose:
	print "SizeOfImage = 0x%08x" % SizeOfImage
  
  #Parse '  0 .text         00000080  00000240  00000240  00000240  2**2'
  #      '                  CONTENTS, ALLOC, LOAD, READONLY, CODE       '
  EndOfTable = data.find("SYMBOL TABLE:")
  Index = data.find("Idx Name")
  End   = data[Index:].find("\n")
  Index = Index + End + 1
  
  PeCoffEnd = 0
  while Index < EndOfTable:
    End   = data[Index:].find("\n")
    Split = data[Index:Index+End].split()
    # Split[0] Indx
    # Split[1] Name i.e. .text
    # Split[2] Size
    # Split[3] VMA
    # Split[4] LMA
    # Split[5] File Off
    # Split[6] Align
    if int(Split[3],16) != int(Split[5],16):
      print "FAIL - %s VMA %08x not equal File off %08x XIP will not work" % (Split[1], int(Split[3],16), int(Split[5],16))
      sys.exit(-1)
  
    if int(Split[3],16) + int(Split[2],16) > PeCoffEnd:
      PeCoffEnd = int(Split[3],16) + int(Split[2],16)
    
    if Split[1] == ".text":
      SecStart = int(Split[3],16)
      SecEnd = int(Split[3],16) + int(Split[2],16)
      if (EntryPoint < SecStart) or (EntryPoint > SecEnd):
        print "FAIL - Entry point (0x%x) not in .text section (0x%x - 0x%x)" % (EntryPoint, SecStart, SecEnd)
        sys.exit(-1)
    
    if Verbose:
		print "%10s" % Split[1] + ' ' + Split[2] + ' ' + Split[3] + ' ' + Split[4] + ' ' + Split[5] + " End = %x" % PeCoffEnd
    Index += data[Index:].find("\n") + 1
    Index += data[Index:].find("\n") + 1
  
  if SizeOfImage < PeCoffEnd:
    print "FAIL - PE/COFF Header SizeOfImage (0x%x) is not correct. Image larger than size (0x%x)." % (SizeOfImage, PeCoffEnd)
    sys.exit(-1)
  
  if Verbose:
	print "\nmach-o relocations:"
  otoolreloc = open("otool-reloc.log", "r")
  lines = otoolreloc.readlines()
  otoolreloc.close()
  
  found = False
  for line in lines:
    if found:
      chunk = line.split()
      if Verbose:
        print chunk[0]
    if line.find ("address") > -1:
      found = True
  
  if Verbose:
	print


if __name__ == "__main__":
    main()
