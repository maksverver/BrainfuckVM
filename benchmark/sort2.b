>>+[>,]<<               read input preceded by marker: 001abcd(e)f000~
[
    >>+                 increment counter
    [
        <-[+<-]<        find and decrement first 1 cell from the right
        [               if not at start:
            >>              move to nonzero neighbour
            [[-<+>]>]       shift sequence to the left
            <<.>            print counter value
        ]
        <
    ]
    >>+         restore marker
    >[->]<+<<   move to back and subtract
]
