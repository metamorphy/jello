# Jello

An *n*×*n* Othello (Reversi) engine written in C in January 1997 by Jeff Mallett.

It was submitted to the [MacTech Magazine](https://en.wikipedia.org/wiki/MacTech) programming contest and won. The contest asked for a player that implemented a fixed `Othello()` entry point, used only the host-provided storage, and played well on even board sizes from 8×8 up to 64×64 under a time budget.

The search is alpha-beta with iterative deepening, a transposition table, a solve extension near the end of the game, futility cut-off, and light selectivity. Timing uses classic Mac Toolbox ticks (`LMGetTicks`).

The contest entry is [`Jello.c`](Jello.c). [`mactech/`](mactech/) has the submitted copy plus links to the published [challenge](http://preserve.mactech.com/articles/mactech/Vol.13/13.02/Feb97Challenge/index.html) (February 1997) and [results](http://preserve.mactech.com/articles/mactech/Vol.13/13.05/May97Challenge/index.html) (May 1997). The misc/ subdirectory holds earlier drafts and a match-driver.

This is historical source. It targets 1990s Macintosh C (CodeWarrior-style `Boolean`, no modern `main` in the contest file) and is not set up as a portable build.
