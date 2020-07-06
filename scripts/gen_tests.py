#! /usr/bin/env python3
import sys;
import os;
from datetime import date

'''
conversion of test cases for consensa and associated sequences
from a given input file into include file format for RNA tests.c

expected input line format: 3 lines per test (ss, pos_vars, sample sequence)
output line format: C code that can be included directly in tests.c
'''

num_tests=0;
outp_indent="    ";

if 2!=len (sys.argv):
    print (sys.argv[0]+': test cases text file expected\n');

def do_test (test_num, test):
    outp=os.popen('./rna --scan --ss="'+test[0]+'" --pos-var="'+test[1]+'" --seq-nt="'+test[2]+'"').read().split ('\n');
    hits=[];
    for i in range(0, len(outp)):
        if (not (outp[i].startswith ("found") or outp[i].startswith ("\n") or len(outp[i])==0)):
            if ("-> " in (outp[i])):
                outp[i]=outp[i][outp[i].rindex ("-> ")+3:];
            this_hit=[];
            this_hit.append (outp[i][:outp[i].index (",")]);
            this_hit.append (outp[i][outp[i].index (",")+1:outp[i].index ("(")]);
            hits.append (this_hit);

    print (outp_indent+"/* test "+str(test_num)+" */");
    print (outp_indent+"strcpy (     ss[t], \""+test[0]+"\");");
    print (outp_indent+"strcpy (pos_var[t], \""+test[1]+"\");");
    print (outp_indent+"strcpy (    seq[t], \""+test[2]+"\");");
    print (outp_indent+"r=0;");
    for i in range(0, len(hits)):
        print (outp_indent+"results[t][r].fp_posn="+hits[i][0]+";  results[t][r++].tp_posn="+hits[i][1]+";");
    print (outp_indent+"num_results[t]=r;");
    print (outp_indent+"assert (r<=MAX_NUM_RESULTS_PER_TEST);");
    print (outp_indent+"t++;\n");


cnt=0;
this_test=[];
this_test_num=1;
with open(sys.argv[1], 'r') as f:
    print (outp_indent+"/*\n"+
           outp_indent+" * auto-generated test cases by "+sys.argv[0]+" on "+str (date.today())+"\n"+
           outp_indent+" */\n");

    this_strn=f.readline ().rstrip ();
    while (this_strn):
        if ('\n'==this_strn and 0==cnt):
            this_strn=f.readline ();
        elif (cnt<3):
            if (this_strn.endswith ('\n')):
                this_strn=this_strn[:-1];
            this_test.append (this_strn);
            this_strn=f.readline ();
            cnt+=1;
        else:
            do_test (this_test_num, this_test);
            cnt=0;
            this_test=[];
            this_test_num+=1;

if cnt==3:
    do_test (this_test_num, this_test);
