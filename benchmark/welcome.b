Problem C: Welcome to Code Jam
Written by linguo for the 2009 Google Code Jam

++++++++++++++++
Memory cell 0 = 16
[>++++>++++++>+++++++>++++++>++>++>>>>+++>++++>++>>>>>>+<<<<<<<<<<<<<<<<<<-]
Memory cells 0 to 18 = 0 64 96 112 96 32 32 0 0 0 48 64 32 0 0 0 0 0 16
>+++>+>+++>+++++>>+++>>>>>------>>>>>>>------>>
Memory cells 0 to 18 = 0 67 97 115 101 32 35 0 0 0 48 58 32 0 0 0 0 0 10 
                  = \@Case#:\@\@0: \@\@\@\@\@\n

At Memory Cell 20 Ready to read in N (one digit at a time subtracting 10 to check for \n and then 48 to get ord(digit character)))
,---------- -------------------------------------->
,----------[ -------------------------------------->
 ,----------[ -------------------------------------->
  ,----------[ This must be a new line since N is at most 100
  ]
  <<<[->>>++++++++++<<<]>>>[-<<<+>>>] Multiply the hundreds digit by 10
  <<[->++++++++++<]>[-<+>] add 10 times previous digit to current digit and then move to previous
 ]
 <<[->++++++++++<]>[-<+>]
]
Now Memory Cell 20 contains N and we are at Memory Cell 21
[
 Memory cell 21 is empty, so this code will never get executed.
 We are going to use 7-memory-cell structures.
 Let us call the 7 cells char_exp, char_obs, tmp0, tmp1, tmp2, cnt0, cnt1

 We are going to use 20 of these structures. 
 The char_exp's of these 20 structures will be constant and will be equal to the characters of '\0welcome to code jam' reversed.
 cnt0,cnt1 will be set to 0,0 for the first 19 structures and 0,1 for the 20th structure at the start of each instance.

 Each time we read a character, we check if it is a newline:
  If it is not, we put the character into char_obs of the first structure. 
  Then we do the following loop over the first 19 structures:
   Copy char_obs to the char_obs of the next structure
   If char_exp != char_obs, set char_obs = 0 and move to the next structure.
   If char_exp = char_obs:
    add cnt0 and cnt1 from the next structure to cnt0 and cnt1 from this structure:
    if cnt1 >= 100, subtract 100 from cnt1 and add 1 to cnt0
    if cnt0 >= 100, subtract 100 from cnt0
    set char_obs = 0 and move to the next structure
  When we reach the final structure, we set its char_obs to be 0 and go back to the start of the loop.

  It is trivial to see that cnt0,cnt1 of the 20th structure will always be 0,1.
  It follows that 100*cnt0+cnt1 of the 19th structure counts the number of w's read so far.
  Similarly, 100*cnt0+cnt1 of the 18th structure counts the number of subsequences equal to 'we' (modulo 10000), and so on.
  Thus, 100*cnt0+cnt1 of the 1st structure gives the required answer.
 If it is a newline:
  We copy the digits from 100*cnt0+cnt1 into the output string, and output, and start again.
]
++++ ++++
++++ ++++
++++ ++++
++++ ++++
[-
       >+++>>>>>>>+++>>>>>>>+++>>>>>>>+
 >>>>>>>+++>>>>>>>+++>>>>>>>+++>>>>>>>+++>>>>>>>+
 >>>>>>>+++>>>>>>>+++>>>>>>>+
 >>>>>>>+++>>>>>>>+++>>>>>>>+++>>>>>>>+++>>>>>>>+++>>>>>>>+++>>>>>>>+++
       <   <<<<<<<   <<<<<<<   <<<<<<<
 <<<<<<<   <<<<<<<   <<<<<<<   <<<<<<<   <<<<<<<
 <<<<<<<   <<<<<<<   <<<<<<<
 <<<<<<<   <<<<<<<   <<<<<<<   <<<<<<<   <<<<<<<   <<<<<<<   <<<<<<<
]
Memory cells 22 up to 148 in steps of 7 = 96 96 96 32 96 96 96 96 32 96 96 32 96 96 96 96 96 96 96 and we are at memory cell 21
      >+++++++++++++
>>>>>>>+
>>>>>>>++++++++++
>>>>>>>
>>>>>>>+++++
>>>>>>>++++
>>>>>>>+++++++++++++++
>>>>>>>+++
>>>>>>>
>>>>>>>+++++++++++++++
>>>>>>>++++++++++++++++++++
>>>>>>>
>>>>>>>+++++
>>>>>>>+++++++++++++
>>>>>>>+++++++++++++++
>>>>>>>+++
>>>>>>>++++++++++++
>>>>>>>+++++
>>>>>>>+++++++++++++++++++++++

 >>>>>>>>>>>>>+<<<<<<<<<<<<< (Set the final count to be 1)

 <<<<<<< <<<<<<< <<<<<<< <<<<<<< <<<<<<< <<<<<<< <<<<<<< <<<<<<< <<<<<<<
 <<<<<<< <<<<<<< <<<<<<< <<<<<<< <<<<<<< <<<<<<< <<<<<<< <<<<<<< <<<<<<<
 <<

[-
 We start at cell 20
 Now change number in case (thrown together very quickly)
 <<<<<<<<<<<<[->+<]+>>
 ------------------------------------------------
 [-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<+<[-]>>]]]]]]]]]
 <<[->>+<<]>>
 ++++++++++++++++++++++++++++++++++++++++++++++++
 <[-<+>[+<->------------------------------------------------[-<+>]]<++++++++++++++++++++++++++++++++++++++++++++++++>]
 <[->+<]>
 [------------------------------------------------<<++++++++++++++++++++++++++++++++++++++++++++++++>>
   [-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<+++++++++++++++++++++++++++++++++++++++++++++++++<[-]++++++++++++++++++++++++++++++++++++++++++++++++>>]]]]]]]]]]
 ]
 <<[->>+<<]>>>>>>>>>>>>>
 Back to cell 20 with number in case increased
 Now clear all of the counts
 >>[>>>>>[-]>[-]>]
 <<<<<<<[<<<<<<<]>>>>>
 Move to cell 23 and take in first character
 >>>,----------[++++++++++
 <[
  >[->+>>>>>>+<<<<<<<]>[-<+>]
  <<[
   >>>>+
   <<<[
    <->->+>>[-]
    <<<[->>+<<]
   ]
   >>[-<<+>>]
   >[<<<<[->>+<<]>+>>>[-]]<<<<
  ]>>[-<<+>>]>>+<<<[>>>-<<<[-]]>>>[
   [-]
   >>>>>>>>[-<<+<<<<<+>>>>>>>]
   >[-<<+<<<<<+>>>>>>>]
   <<[->>+<<]<[->>+<<]<<<<
   [-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>
   [-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>
   [-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>
   [-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>
   [-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>
   [-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>
   [-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>
   [-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>
   [-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>
   [-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[
    -<+<[-]>>[-<<+>>]
   ]]]]]]]]]]
   ]]]]]]]]]]
   ]]]]]]]]]]
   ]]]]]]]]]]
   ]]]]]]]]]]
   ]]]]]]]]]]
   ]]]]]]]]]]
   ]]]]]]]]]]
   ]]]]]]]]]]
   ]]]]]]]]]]
   <<[->>+<<]>>
   <
   [-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>
   [-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>
   [-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>
   [-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>
   [-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>
   [-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>
   [-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>
   [-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>
   [-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>
   [-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[-<+>[
    -<[-]>[-<+>]
   ]]]]]]]]]]
   ]]]]]]]]]]
   ]]]]]]]]]]
   ]]]]]]]]]]
   ]]]]]]]]]]
   ]]]]]]]]]]
   ]]]]]]]]]]
   ]]]]]]]]]]
   ]]]]]]]]]]
   ]]]]]]]]]]
   <[->+<]
  ]
  >>>
 ]
 >[-]<<<<<<<<[<<<<<<<]>>>>>>>>
 ,----------]<<<
 > ++++++++
   ++++++++
   ++++++++
   ++++++++
   ++++++++
   ++++++++ [-<<<<+<+<+<+>>>>>>>]
 >>>>>>[-<<<+>>>[-<<<+>>>[-<<<+>>>[-<<<+>>>[-<<<+>>>[-<<<+>>>[-<<<+>>>[-<<<+>>>[-<<<+>>>[-<<<<+>[-]>>>[-<<+>>]]]]]]]]]]<<[->>+<<]>>]
 >[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<+>>[-<<<+>[-]>>[-<+>]]]]]]]]]]<[->+<]>]
 <<
 [-<<<<<<<<<+>>>>>>>>>]<
 [-<<<<<<<<<+>>>>>>>>>]<
 [-<<<<<<<<<+>>>>>>>>>]<
 [-<<<<<<<<<+>>>>>>>>>]<
 <
 We end at cell 21 so display answer and clear
 < <<<<< <<<<< <<<<< <<<<
 .>.>.>.>.>.>>[.[-]]>[.[-<+>]]>.>.>.>[.[-]]>[.[-]]>[.[-]]>[.[-]]>[.[-]]>.>>
]
