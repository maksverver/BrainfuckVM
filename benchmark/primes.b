[ A reasonably efficient Brainfuck program to generate a truly infinite
  sequence of prime numbers (provided sufficient time and space is available).

 (Note: can be made twice as fast by only doing odd numbers.)

  Numbers are represented by 4-cell records of the form: 1 d 0 0, where d is a
  base 10 digit, and the zeros may be used as temporary scratch space.

  The tape consists of a sequence of prime numbers (minus one) represented in
  this variable length format and then the next number to be tested in the same
  format, but separated from the list by another zero cell.

  00aa0bb0ccc00ddd  (where a,b,c,d represent e.g. 2,3,5,6 respectively
                     note the double 0 before d-cells.)

  For prime numbers, the first scratch cell is not zero but instead contains
  a countdown until the current number is multiple of this prime again.  This
  can be used to determine when counter reaches a prime number.

  Zero records have a first byte zero, but some of the other cells are borrowed
  to keep track of the primality of the counter.  In particular, since for all
  primes the predecessor is stored (e.g. 4 instead of 5), which is easier when
  resetting those counters, the prime counter is printed in the iteration after
  it is copied.
]

>>>>
>>+>>       Mark first number as prime
>>>>+       Initialize counter to zero

[

    Increment current number by one: (from left to right)
    >>+<<
    [
        >>[-
            <+[->+>+<<]   increment and copy current digit
            >>
            -[-[-[-[-[-[-[-[-[-
                <[-]>
                >[-]+>>+<<<
            ] ] ] ] ] ] ] ] ]

            <[-<+>]
        ]>>
    ]
    <<<<[<<<<]

    <<<[->>>

        Print current number
        >>>>[>>>>]<<<<
        [>
            >++++++[-<++++++++>]<
            .
            >++++++[-<-------->]<
        < <<<<]
        ++++++++++.[-] newline

    <<<]>[->>

        Copy current number to list of primes

        Move all digits one cell to the right to create an empty cell
        >>>>[>>>>]<<<<
        [->[->>>>+<<<<]>>>+<<<< <<<<]>>>> >>>>
        [
            Clone digit to cell on the right and mark it as copied
            >[->+>+<<]>>[-<<+>>]+<<<

            Move digit to the left
            [>>[-<<<<+>>>>]<< <<<<]

            Move old digits to the right
            >>>>[>>>>]<<<<[ ->[->>>>+<<<<]>>[->>>>+<<<<]>+<<<< <<<<]

            Create new digit:
            <<<<+>>>> >>[-<<<<+<+>>>>>]>>

            Move to the start of the first uncopied digit
            >>>> >>>[>>>>]<<<
        ]

        Remove copied marks:
        <[-<<<<]<<<

        Set a bit so that next number is printed
        <<<+>>>

    <<]<<


    Move to front of list of prime residues:
    <<<< [ [<<<<]<<<< ] >>>>

    We keep track of a single bit that is 0 if all residues were
    positive (if this is the case the next number will be prime)
    >>+>>

    Subtract one from each prime residue:
    [
        Copy prime bit over:
        <<[ ->>[>>>>]>>+<<  <<<<[<<<<]>> ]>>

        Decrement:
        >>>+<<<[
            >>>[-
                >>>>+<<<<
                +++++++++
                <[>>>>>-<<<<< >[-]< -[->+<]]>
                [-<+>]
            ]<<<
        >>>>]

        Check for underflow:
        >>>[-

            Clear prime bit:
            <[-]<<

            Reset number:
            <<<<[<<<<]>>>>
            [>>[-]<[->+>+<<]>>[-<<+>>]>]
        >>>]<<<

        >>>>
    ]
    >>>>
]
