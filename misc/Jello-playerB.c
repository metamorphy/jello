/*
 *  Jello: an nxn Othello program by Jeff Mallett 1/97
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MY_ASSERT(b) 


// PARAMETERS

	// Extensions/Pruning
	#define kFullDepthPlies			2 // # of full-depth plies before selectivity
	#define kSolveThreshold			4 // Plies extension for solving whole board
	#define kMinimumSafeDisks		3 // Minimum disks to have and still be safe
	#define kFutilityScore			kCorner // Score above beta to trigger futility cut-off
	#define kWipeOutExtension   2 // Extend a ply if opponent doesn't have more disks than this

	// ###
	#define kOpeningMoveTime		4 // Average move time in 60ths of a second
	#define kMoveTime						7 // Average move time in 60ths of a second

	// Scoring Parameters
	#define kFinalDisk		50	// per disk on board at the end
	#define kTooFewDisks	-50 // per disk under threshold


// Array of squares on the board (up to 66 x 66 squares)
// Each square has:
//    1st 8 bits -- In this direction there's a BLACK disk that can be flipped by WHITE
#define ADJACENCY					0x000000FF // Adjacent to disk in 8 directions
#define BORDER_ADJACENCY	0x0000FF00 // Adjacent to border in 8 directions
#define WHITE							0x00010000 // Is White disk here?
#define BLACK							0x00020000 // Is Black disk here?
#define COLOR_BITS				0x00030000 // Mask: Is disk here?
#define BORDER_BIT				0x00040000 // Is this border square?
#define COLOR_BORDER_BITS	0x00070000 // Mask: Is border or disk here?
#define BAD_BIT						0x00080000 // X or C square
#define EMPTYADJ_BITS			0xFFF00000 /* Top 12 bits (0-4095) */

#define BORDER_ADJACENCY_SHIFT	8
#define COLOR_SHIFT							16
#define EMPTYADJ_SHIFT					20

#define IS_NOT_EMPTY(z)			((z) & COLOR_BORDER_BITS)
#define IS_EMPTY(z)					!IS_NOT_EMPTY(z)
#define HAS_DISK(z)					((z) & COLOR_BITS)
#define HAS_NO_DISK(z)			!HAS_DISK(z)
#define IS_BORDER(z)				((z) & BORDER_BIT)
#define IS_ON_BOARD(z)			!IS_BORDER(z)
#define IS_BAD(z)						((z) & BAD_BIT)
#define IS_EDGE(z)					((z) & BORDER_ADJACENCY)
#define IS_CORNER(z)				(gCountZeros[((z) & BORDER_ADJACENCY) >> BORDER_ADJACENCY_SHIFT] == 3)

#define OPPONENT(side)			((side) ^ COLOR_BITS)

#define XY2INDEX(x, y)			((y) * gRealBoardSize + (x))

#define COUNT_EMPTIES(z) \
	gCountZeros[ ((z) | ((z) >> BORDER_ADJACENCY_SHIFT)) & ADJACENCY ]

#define DIR_BIT(dir)				(1L << (dir))
#define OPP_DIR_BIT(dir)		gOppDirBit[dir]

#define EMPTYADJ_BIT(index)				((index) << EMPTYADJ_SHIFT)
#define GET_EMPTYADJ_INDEX(pSq)		(*(pSq) >> EMPTYADJ_SHIFT)
#define SET_EMPTYADJ_INDEX(pSq, index) \
	*(pSq) = (*(pSq) & ~EMPTYADJ_BITS) | EMPTYADJ_BIT(index)

// Add to end of list
#define ADD_TO_EMPTYADJ(pSq) \
	SET_EMPTYADJ_INDEX(pSq, gSizeEmptyAdj); \
	gEmptyAdj[gSizeEmptyAdj++] = pSq

// Swap entry in list with end entry and shrink list
#define REMOVE_FROM_EMPTYADJ(pSq) { \
	long i = GET_EMPTYADJ_INDEX(pSq); \
	unsigned long *p = gEmptyAdj[--gSizeEmptyAdj]; \
	gEmptyAdj[i] = p; \
	SET_EMPTYADJ_INDEX(p, i); \
}
	

#define PUSH(x)				*(gChangesEnd++) = (x)
#define START_SAVE		PUSH(0L)
#define PUSH_SQ(pSq) 	{ PUSH(*(pSq)); PUSH((unsigned long)(pSq)); }
#define POP						*(--gChangesEnd)
#define TOP						*gChangesEnd

typedef unsigned long * PSQUARE;
typedef long SCORE;
#define kInfinity			110000000L
#define kBestPossible	100000000L // ### Should use kInfinity for immediate break outs?

// Directions (if se increases both x and y):
//     7  6  5  4  3  2  1  0
//    NE SW NW SE  N  S  W  E  (opposites are adjacent)
enum { DIR_E = 0, DIR_W, DIR_S, DIR_N, DIR_SE, DIR_NW, DIR_SW, DIR_NE };
enum { E  = 0x0001,
       W  = 0x0002,
       S  = 0x0004,
       N  = 0x0008,
       SE = 0x0010,
       NW = 0x0020,
       SW = 0x0040,
       NE = 0x0080,
       
       E_BORDER  = 0x0100,
       W_BORDER  = 0x0200,
       S_BORDER  = 0x0400,
       N_BORDER  = 0x0800,
       SE_BORDER = 0x1000,
       NW_BORDER = 0x2000,
       SW_BORDER = 0x4000,
       NE_BORDER = 0x8000
};

#define USE_HASH
#ifdef USE_HASH
	#define kHashTableMask		0x7FFF // 32K entry table
	#define kHashTableSize		(kHashTableMask + 1)
	#define kSwitchSideHash		0x87654321
	enum { INVALID = 0, VALID = 1, LOWER_BOUND = 2, UPPER_BOUND = 3 };

	typedef struct SHash {
		unsigned long HashValue;
		SCORE Score;
		PSQUARE BestMove;
		short Depth;
		short Type;
	} SHash;
	
	static SHash *gHashTable;
	static long gWhiteHashOffset, gBlackHashOffset, gFlipHashOffset;
	static unsigned long gHashValue;
#endif


// ***********************************************************
// Prototypes
// ***********************************************************

Boolean /*legalMove*/ OthelloB (
  long boardSize,    /* number of rows/columns in the game board */
  long oppRow,       /* row where opponent last moved, 0 .. boardSize-1 */
  long oppCol,       /* column where opponent last moved, 0 .. boardSize-1 */
  long *moveRow,     /* return your move - row, 0 .. boardSize-1 */
  long *moveCol,     /* return your move - column, 0 .. boardSize-1 */
  void *privStorage, /* preallocated storage for your use */
  long storageSize,  /* size of privStorage in bytes */
  Boolean newGame,   /* TRUE if this is your first move in a new game */
  Boolean playBlack  /* TRUE if you play first (black pieces) */
);

static SCORE Search(SCORE alpha, SCORE beta, unsigned long side, long depth, Boolean passEndsGame);
static long Generate(unsigned long side);
static SCORE MakeMove(PSQUARE to, unsigned long side);
static void UnmakeMove();
static void Initialize(long boardSize, void *privStorage);
static SCORE Evaluate(unsigned long side);
static long SortValue(PSQUARE p, unsigned long side);
static void BubbleSort(long n, unsigned long side);


// ***********************************************************
// Static
// ***********************************************************

// Direction indices
static unsigned long gOppDirBit[8] = { W, E, N, S, NW, SE, NE, SW };

// Offsets on board plus zero sentinel
// Fill in gOffsets[2..7] when we know board size
static long gOffsets[9] = {1L,-1L,99L,99L,99L,99L,99L,99L,0L};

// gSquares -- Array of squares: Stores board
static unsigned long *gSquares; 
static unsigned long *gOnBoardStart; // Pointer into gSquares
static unsigned long *gOnBoardEnd; // Pointer into gSquares
static long gNumOnSquares; // # of squares, not including borders
static long gRealBoardSize; // Length of a side (includes borders)

// gEmptyAdj -- Array of pointers to squares: Stores all empty squares adjacent to disk(s)
static PSQUARE *gEmptyAdj; 
static long gSizeEmptyAdj;

// gChanges -- Array of unsigned longs containing data to undo moves
//     list of:
//          <pointer to square> <old square value>
//     terminated by a OL
//   The first position will be the drop square and the others will be flips
static unsigned long *gChanges;
static unsigned long *gChangesEnd; // Pointer into gChanges

// gCountZeros -- Precalculated 256-element constant array
//     returns count of zero bits in the byte
static unsigned long *gCountZeros;

// gTree -- Array of pointers to squares: Holds search tree
static PSQUARE *gTree;
static PSQUARE *gTreeEnd; // Pointer into gTree

// gMobility -- Array of counts of moves available at each ply
static long *gMobility;

// Pointers to various special squares
static PSQUARE gNWCorner, gNWX, gNWC1, gNWC2;
static PSQUARE gNECorner, gNEX, gNEC1, gNEC2;
static PSQUARE gSWCorner, gSWX, gSWC1, gSWC2;
static PSQUARE gSECorner, gSEX, gSEC1, gSEC2;

// The gCounts array holds current counts of disks
// Access is by gCounts[side >> COLOR_SHIFT] 
//   gCounts[WHITE_INDEX] white's disks
//   gCounts[BLACK_INDEX] black's disks
#define WHITE_INDEX		(WHITE >> COLOR_SHIFT)
#define BLACK_INDEX		(BLACK >> COLOR_SHIFT)
static unsigned long gCounts[3];

static SCORE gIncScore; // Score relative to side to move
static SCORE gEndgameDiskBonus; // Bonus per disk in endgame
static SCORE kX, kC, kEdge, kCorner; // Penalties/Bonuses

static long gAbortSearchTime; // Time at which the search will be aborted
static Boolean gAborted; // Has the search been aborted?

static long gStartDepth; // Search was started at this depth
static long gPly; // Number of moves deep



// ***********************************************************
// Othello
// ***********************************************************
Boolean /*legalMove*/ OthelloB (
  long boardSize,    /* number of rows/columns in the game board */
  long oppRow,       /* row where opponent last moved, 0 .. boardSize-1 */
  long oppCol,       /* column where opponent last moved, 0 .. boardSize-1 */
  long *moveRow,     /* return your move - row, 0 .. boardSize-1 */
  long *moveCol,     /* return your move - column, 0 .. boardSize-1 */
  void *privStorage, /* preallocated storage for your use */
  long storageSize,  /* size of privStorage in bytes */
  Boolean newGame,   /* TRUE if this is your first move in a new game */
  Boolean playBlack  /* TRUE if you play first (black pieces) */
)
{
	PSQUARE *to, p;
	unsigned long side = playBlack ? BLACK : WHITE;
	unsigned long nextSide;
	SCORE t, bestScore, saveIncScore;
	long j, generated, index, x, y;
	long stillOpen, bestFoundAt, *pOffsets;
	long startTime, targetTime, targetDuration;
	Boolean nearEdge;
#ifdef USE_HASH
	unsigned long saveHashValue;
#endif

	if (newGame) {
		Initialize(boardSize, privStorage);
	}
	gChangesEnd = gChanges;
	
#ifdef USE_HASH
	// Fix up gHashTable
	{
		SHash *pHashTable = gHashTable;
		int i;
		
		i = kHashTableSize - 1;
		do {
			(pHashTable++)->Depth -= 2;
		} while (i--);
	}
#endif

	if (oppRow != -1) {
		index = XY2INDEX(oppCol+1, oppRow+1);
		gIncScore -= MakeMove(&gSquares[index], playBlack ? WHITE : BLACK);
		gChangesEnd = gChanges;
	}
	
	gTreeEnd = gTree;
	generated = Generate(side);
	
	if (!generated) { // No moves
		*moveRow = *moveCol = -1;
		return TRUE;
	}
	
	if (generated > 1 && !(newGame && oppRow == -1)) {
		BubbleSort(generated, side);
		
		gMobility[0] = gMobility[1] = generated;
		gPly = 1;
		
		nextSide = OPPONENT(side);
		stillOpen = gNumOnSquares - gCounts[WHITE_INDEX] - gCounts[BLACK_INDEX];

		// Calculate gEndgameDiskBonus
		gEndgameDiskBonus = 0;
		x = gNumOnSquares / 3;
		j = x - stillOpen;
		if (j > 0) {
			gEndgameDiskBonus = (j * kFinalDisk) / (x * 5);
			if (!gEndgameDiskBonus)
				gEndgameDiskBonus = 1;
		}

		// Do we have any pieces near an edge?
		nearEdge = false;
		for (p = gOnBoardStart; !nearEdge && p <= gOnBoardEnd; ++p) {
			if (HAS_DISK(*p)) {
				if (IS_EDGE(*p)) {
					nearEdge = true;
				} else {
					pOffsets = gOffsets;
					do {
						if (IS_EDGE(*(p + *pOffsets)))
							nearEdge = true;
					} while (!nearEdge && *(++pOffsets));
				}
			}
		}
		
		// Set times
		if (!nearEdge)
			targetDuration = kOpeningMoveTime;
		else if (stillOpen > 20)
			targetDuration = kMoveTime;
		else // try to solve!
			targetDuration = kMoveTime * 8;
		startTime = LMGetTicks();
		targetTime = startTime + targetDuration;
		gAbortSearchTime = startTime + targetDuration * 6;
		gAborted = false;

		saveIncScore = gIncScore;
#ifdef USE_HASH
		saveHashValue = gHashValue;
#endif

		for (gStartDepth=1; gStartDepth < stillOpen && !gAborted; ++gStartDepth) {
			to = gTree;
			bestScore = -kInfinity;
			for (j=0; j<generated; ++j) {
				gIncScore = - (gIncScore + MakeMove(*to, side));
					++gPly;
					t = -Search(-kInfinity, kInfinity, nextSide, gStartDepth - 1, false);
					--gPly;
				UnmakeMove();
				gIncScore = saveIncScore;
#ifdef USE_HASH
				gHashValue = saveHashValue;
#endif
				if (gAborted)
					break;
					
				if (t > bestScore) {					
					PSQUARE bestMove, *p;
					// Move *to to front of the tree
					bestMove = *to;
					MY_ASSERT(bestMove >= gOnBoardStart && bestMove <= gOnBoardEnd);
					for (p = to; p != gTree; --p)
						*p = *(p-1);
					*gTree = bestMove;
					
					bestScore = t;
					bestFoundAt = gStartDepth;
				}
				if (LMGetTicks() >= targetTime) {
					if (bestScore > -kInfinity && gStartDepth + kSolveThreshold + 1 != stillOpen)
						break; // time to stop
					if (LMGetTicks() - startTime >= 3 * targetDuration)
						break; // time to stop
				}
				++to;
			}
			
			if (gStartDepth >= 4 && LMGetTicks() - startTime > 1 + targetDuration/2)
				break; // probably not enough time to finish another iteration
			if (gStartDepth - bestFoundAt == 3 && IS_CORNER(*gTree[0]))
				break; // easy corner move
		}
	}
	
	gIncScore += MakeMove(*gTree, side);
	index = (long)(*gTree - gSquares);
	y = index / gRealBoardSize;
	x = index - y * gRealBoardSize;
	*moveCol = x - 1;
	*moveRow = y - 1;
	//###
	{
		extern long countB, countW;
		countW = gCounts[WHITE_INDEX];
		countB = gCounts[BLACK_INDEX];
	}
	
	return TRUE;
}

// ***********************************************************
// SortValue
// ***********************************************************
// Returns value that orders squares for root search
long SortValue(PSQUARE p, unsigned long side)
{
	long stillOpen, value;
	PSQUARE q;
	unsigned long occupant, enemy;
	long *pOffsets;
	
	if (IS_EDGE(*p)) {
		if (IS_CORNER(*p))
			return 200; // Corner
		if (IS_BAD(*p))
			return -100; // C
		return 100; // Edge
	}
	if (IS_BAD(*p))
		return -200; // X
	
	// Check p's adjacency bits
	stillOpen = gNumOnSquares - gCounts[WHITE_INDEX] - gCounts[BLACK_INDEX];
	enemy = OPPONENT(side);
	value = 0;
	pOffsets = gOffsets;
	do {
		q = p + *pOffsets;
		occupant = *q & COLOR_BITS;
		if (stillOpen > 10) {
			if (occupant == side)
				++value; // good to take away empty-adjacent
			else if (occupant == enemy)
				--value; // bad to flip
		} else if (occupant == OPPONENT(side))
			++value; // endgame: good to flip
	} while (*(++pOffsets));
	
	return value;
}

// ***********************************************************
// BubbleSort
// ***********************************************************
// Sorts generated root moves in decreasing value order
// Hey, bubble sort is really okay in this case
void BubbleSort(long n, unsigned long side)
{
	long i, j, swaps;
	PSQUARE temp;
	
	for (j=n-2; j>=0; --j) {
		swaps = 0;
		i = 0;
		do {
			if (SortValue(gTree[i+1], side) > SortValue(gTree[i], side)) {
				++swaps; // Swap i and i+1
				temp = gTree[i];
				gTree[i] = gTree[i+1];
				gTree[i+1] = temp;
			}
		} while (i++ != j);
		if (!swaps)
			break; // Already sorted: Finish early
	}
}

// ***********************************************************
// Evaluate
// ***********************************************************
// Evaluate position and return score relative to side
// side is also the side to move
SCORE Evaluate(unsigned long side)
{
	SCORE score = 0;
	
	// Maximize disks, but only in endgame
	if (gEndgameDiskBonus) {
		score = (gCounts[WHITE_INDEX] - gCounts[BLACK_INDEX]) * gEndgameDiskBonus;
		if (side == BLACK)
			score = -score;
	}

	// NW Corner Area
	if (HAS_DISK(*gNWCorner)) {
		score += (*gNWCorner & side) ? kCorner : -kCorner;
	} else if (*gNWCorner & ADJACENCY) {
		if (HAS_DISK(*gNWX)) {
			score += (*gNWX & side) ? kX : -kX;
		}
		if (HAS_DISK(*gNWC1)) {
			score += (*gNWC1 & side) ? kC : -kC;
		}
		if (HAS_DISK(*gNWC2)) {
			score += (*gNWC2 & side) ? kC : -kC;
		}
	}

	// NE Corner Area
	if (HAS_DISK(*gNECorner)) {
		score += (*gNECorner & side) ? kCorner : -kCorner;
	} else if (*gNECorner & ADJACENCY) {
		if (HAS_DISK(*gNEX)) {
			score += (*gNEX & side) ? kX : -kX;
		}
		if (HAS_DISK(*gNEC1)) {
			score += (*gNEC1 & side) ? kC : -kC;
		}
		if (HAS_DISK(*gNEC2)) {
			score += (*gNEC2 & side) ? kC : -kC;
		}
	}

	// SW Corner Area
	if (HAS_DISK(*gSWCorner)) {
		score += (*gSWCorner & side) ? kCorner : -kCorner;
	} else if (*gSWCorner & ADJACENCY) {
		if (HAS_DISK(*gSWX)) {
			score += (*gSWX & side) ? kX : -kX;
		}
		if (HAS_DISK(*gSWC1)) {
			score += (*gSWC1 & side) ? kC : -kC;
		}
		if (HAS_DISK(*gSWC2)) {
			score += (*gSWC2 & side) ? kC : -kC;
		}
	}

	// SE Corner Area
	if (HAS_DISK(*gSECorner)) {
		score += (*gSECorner & side) ? kCorner : -kCorner;
	} else if (*gSECorner & ADJACENCY) {
		if (HAS_DISK(*gSEX)) {
			score += (*gSEX & side) ? kX : -kX;
		}
		if (HAS_DISK(*gSEC1)) {
			score += (*gSEC1 & side) ? kC : -kC;
		}
		if (HAS_DISK(*gSEC2)) {
			score += (*gSEC2 & side) ? kC : -kC;
		}
	}
	
	// Too few disks?
	if (gCounts[WHITE_INDEX] < kMinimumSafeDisks) {
		SCORE x = (kMinimumSafeDisks - gCounts[WHITE_INDEX]) * kTooFewDisks;
		score += (side == WHITE) ? 2 * x : -x;
	}
	if (gCounts[BLACK_INDEX] < kMinimumSafeDisks) {
		SCORE x = (kMinimumSafeDisks - gCounts[BLACK_INDEX]) * kTooFewDisks;
		score += (side == BLACK) ? 2 * x : -x;
	}
	
	score += gMobility[gPly-2] - gMobility[gPly-1];
	
	return gIncScore + score; //### also right to move
}

// ***********************************************************
// Search
// ***********************************************************
SCORE Search(SCORE alpha, SCORE beta, unsigned long side, long depth, Boolean passEndsGame)
{
	register PSQUARE *to;
	unsigned long nextSide;
	long generated;
	SCORE t, bestScore, saveIncScore;
	PSQUARE *firstMove;
	long stillOpen;
	SCORE oldAlpha = alpha;
#ifdef USE_HASH
	unsigned long saveHashValue;
	PSQUARE bestMove, iTryMove;
	SHash *pHashTable;
#endif	
	
	stillOpen = gNumOnSquares - gCounts[WHITE_INDEX] - gCounts[BLACK_INDEX];
	if (!stillOpen) {
		// Board full
		bestScore = (gCounts[WHITE_INDEX] - gCounts[BLACK_INDEX]) * kFinalDisk;
		if (side == BLACK)
			bestScore = -bestScore;
		return bestScore;
	}
	
	if (!gCounts[WHITE_INDEX]) {
		// White is wiped out!
		bestScore = kBestPossible;
		if (side == WHITE)
			bestScore = -kBestPossible;
		return bestScore;
	}
	
	if (!gCounts[BLACK_INDEX]) {
		// Black is wiped out!
		bestScore = kBestPossible;
		if (side == BLACK)
			bestScore = -kBestPossible;
		return bestScore;
	}
	
	if (depth <= 0) {
		if (stillOpen > kSolveThreshold &&
				gCounts[OPPONENT(side) >> COLOR_SHIFT] > kWipeOutExtension) {
			bestScore = Evaluate(side);
#ifdef USE_HASH
			bestMove = NULL;
#endif	
			goto HashSave; // Terminal node
		}
	} else if (depth == 1 && stillOpen > kSolveThreshold) {
		t = Evaluate(side);
		if (t > beta + kFutilityScore)
			return t; // Futility cut-off
	}

#ifdef USE_HASH
	iTryMove = NULL;
	// Check hash table
	pHashTable = &gHashTable[gHashValue & kHashTableMask];
	if (pHashTable->HashValue == gHashValue) {
		//Get best move and use for move ordering
		iTryMove = pHashTable->BestMove;
		if (pHashTable->Depth >= depth) {
			if (pHashTable->Type == VALID) {
				if (pHashTable->Score > beta)
			   	alpha = beta;
				else if (pHashTable->Score > alpha)
					alpha = pHashTable->Score;
			} else if (pHashTable->Type == LOWER_BOUND) {
				if (pHashTable->Score > beta)
					return beta + 1;
			} else if (pHashTable->Type == UPPER_BOUND) {
				if (pHashTable->Score < alpha)
					return alpha - 1;
			}
			if (alpha > beta)
				return beta;
		}
	}
#endif
	
	// Abort?
	if (LMGetTicks() >= gAbortSearchTime) {
		gAborted = true;
		return 0;
	}

#ifdef USE_HASH
	bestMove = NULL;
#endif	
	nextSide = OPPONENT(side);

	firstMove = gTreeEnd;
	generated = Generate(side);
	gMobility[gPly] = generated;
	
	if (!generated) { // no moves available
		if (passEndsGame) {
			bestScore = (gCounts[WHITE_INDEX] - gCounts[BLACK_INDEX]) * kFinalDisk;
			if (side == BLACK)
				bestScore = -bestScore;
			return bestScore;
		}
#ifdef USE_HASH
		gHashValue ^= kSwitchSideHash;
#endif
		gIncScore = -gIncScore;
		++gPly;
			bestScore = -Search(-beta, -alpha, nextSide, depth, true);
		--gPly;
		gIncScore = -gIncScore;
		++depth;
		goto Searched;
 	}

	to = firstMove;
	bestScore = -kInfinity;

#ifdef USE_HASH	
	if (iTryMove && *to != iTryMove) {
		// Find iTryMove in move list and move to front
		PSQUARE *p;
		for (p = to + 1; *p; ++p)
			if (*p == iTryMove) {
				// Swap *p and *to
				*p = *to;
				*to = iTryMove;
				break;
			}
	}
#endif

	saveIncScore = gIncScore;
#ifdef USE_HASH
	gHashValue ^= kSwitchSideHash;
	saveHashValue = gHashValue;
#endif

	do {
		if (!IS_BAD(**to) || // selectivity
#ifdef USE_HASH
					!bestMove ||
#endif
					gStartDepth - depth <= kFullDepthPlies ||
					stillOpen <= kSolveThreshold) {
			gIncScore = - (gIncScore + MakeMove(*to, side));
			++gPly;
				t = -Search(-beta, -alpha, nextSide, depth-1, false);
			--gPly;
			UnmakeMove();
			gIncScore = saveIncScore;
#ifdef USE_HASH
			gHashValue = saveHashValue;
#endif

			if (gAborted) {
#ifdef USE_HASH
				bestMove = NULL;
#endif
				break;
			}
			
			if (t > bestScore) {
#ifdef USE_HASH
				bestMove = *to;
#endif
				if (t > alpha) {
					if (t >= beta) {
						bestScore = t;
						break;
					}
					alpha = t;
				}
				bestScore = t;
			}
		}
		++to;		
	} while (*to);

Searched: ;
#ifdef USE_HASH
	gHashValue ^= kSwitchSideHash;
#endif
	gTreeEnd = firstMove;

#ifdef USE_HASH
	if (bestMove) {
#endif
HashSave: ;
#ifdef USE_HASH
		pHashTable = &gHashTable[gHashValue & kHashTableMask];
		if (pHashTable->Depth <= depth) {
			pHashTable->HashValue = gHashValue;
			pHashTable->Depth = depth;
			pHashTable->Score = bestScore;
			pHashTable->BestMove = bestMove;
			MY_ASSERT(!bestMove || IS_EMPTY(*bestMove));
			
			pHashTable->Type = VALID;
			if (bestScore <= oldAlpha) {
				pHashTable->Type = UPPER_BOUND;
			} else if (bestScore >= beta) {
				pHashTable->Type = LOWER_BOUND;
			}
		}
	}
#endif

	return bestScore;
}

// ***********************************************************
// Generate
// ***********************************************************
#define ADD_MOVE(pSq)		*(gTreeEnd++) = pSq
long Generate(unsigned long side)
{
	PSQUARE *e, p, q, *afterCorners, *movesStart, lastBad;
	unsigned long enemy;
	long i, *pOffsets;
	
	enemy = OPPONENT(side);
	afterCorners = movesStart = gTreeEnd;
	lastBad = NULL;
		
	e = gEmptyAdj;
	i = gSizeEmptyAdj;
	while (i--) {
		p = *e;
		MY_ASSERT(IS_EMPTY(*p) && (*p & ADJACENCY));
		pOffsets = gOffsets;
		do {
			q = p + *pOffsets;
			if (*q & enemy) {
				do { // Skip through line of enemy disks
					q += *pOffsets;
				} while (*q & enemy);
				if (*q & side) { // Add square p to move list!
					// Bad?  Try to put it on the end
					if (IS_BAD(*p)) {
						if (!lastBad) {
							lastBad = p;
							break;
						}
						ADD_MOVE(p);
						break;
					}
					if (!IS_CORNER(*p)) {
						// Add to end after corners
						ADD_MOVE(p);
						break;
					}
					// Corner: keep all corners on front
					if (afterCorners == gTreeEnd) { // All are corners!
						ADD_MOVE(p);
					} else {
						unsigned long *temp = *afterCorners;
						*afterCorners = p;
						ADD_MOVE(temp);
					}
					++afterCorners;
					break;
				}
			}
		} while (*(++pOffsets));
		++e;
	}
	if (lastBad) {
		ADD_MOVE(lastBad);
	}
	ADD_MOVE(NULL); // sentinel
	return (long)(gTreeEnd - movesStart) - 1;
}

// ***********************************************************
// MakeMove
// ***********************************************************
// Makes the move for "side" on the "to" square
// Saves the move to gChanges for later undo'ing
// Returns the change in score relative to the moving side
SCORE MakeMove(PSQUARE to, unsigned long side)
{
	unsigned long z;
	PSQUARE q, p;
	long dir, offset;
	long flips = 0;
	SCORE score = 0;
	unsigned long enemy = OPPONENT(side);

	MY_ASSERT(IS_EMPTY(*to) && (*to & ADJACENCY));
	
#ifdef USE_HASH
	// Update hash for new disk
	if (side == BLACK)
		gHashValue ^= *(to + gBlackHashOffset);
	else
		gHashValue ^= *(to + gWhiteHashOffset);
#endif

	START_SAVE;
	
	// Do flipping, Update adjacency
	dir = 7;
	do {
		offset = gOffsets[dir];
		q = to + offset;
		if (IS_ON_BOARD(*q)) {			
			if (IS_EMPTY(*q)) {
				if ( !(*q & ADJACENCY) ) { // First adjacent
					ADD_TO_EMPTYADJ(q);
				}
				score--; // New disk touches empty, which is bad
			} else if (*q & enemy) {
				// Skip through line of enemy disks
				p = q + offset;
				while (*p & enemy)
					p += offset;
					
				if (*p & side) { // Flip 'em
					p = q;
					do {
						// Flip disk at p
						++flips;
						if (IS_EDGE(*p))
							score += kEdge;
						PUSH_SQ(p);
						score -= (COUNT_EMPTIES(*p) << 1); // x2  Empties counted for us now
						*p ^= COLOR_BITS; // Flip
#ifdef USE_HASH
						gHashValue ^= *(p + gFlipHashOffset); // Update hash for flipped disk
#endif
						p += offset;
					} while (*p & enemy);
					score++; // Fills in area around disk at q which is now ours
				} else {
					score--; // Fills in area around disk at enemy disk at q
				}
			} else { // *q & side
				score++; // Fills in area around our disk at q
			}
			z = OPP_DIR_BIT(dir);
			MY_ASSERT((*q & z) == 0L);
			*q |= z;
		}
	} while (dir--);

	PUSH_SQ(to);
	REMOVE_FROM_EMPTYADJ(to);
	*to |= side;

	PUSH(gCounts[WHITE_INDEX]);
	PUSH(gCounts[BLACK_INDEX]);
	gCounts[side >> COLOR_SHIFT] += flips + 1;
	gCounts[enemy >> COLOR_SHIFT] -= flips;
	
	if (IS_EDGE(*to))
		score += kEdge;
	
	return score;
}

// ***********************************************************
// UnmakeMove
// ***********************************************************
void UnmakeMove()
{	
	PSQUARE to, flipped, q;
	unsigned long z;
	long dir;
	
	// Restore disk counts
	gCounts[BLACK_INDEX] = POP;
	gCounts[WHITE_INDEX] = POP;

	// Replace to-disk
	to = (PSQUARE)POP;
	*to = POP;
	ADD_TO_EMPTYADJ(to);

	// Undo disk changes
	while (POP) {
		flipped = (PSQUARE)TOP;
		*flipped = POP;
	}

	// Update adjacency
	dir = 7;
	do {
		q = to + gOffsets[dir];
		z = OPP_DIR_BIT(dir);
		*q &= ~z; // Remove adjacency (if not already removed from disk changes)
		if ( IS_EMPTY(*q) && !(*q & ADJACENCY) ) { // First adjacent
			REMOVE_FROM_EMPTYADJ(q);
		}
	} while (dir--);
}

// ***********************************************************
// Initialize
// ***********************************************************
void Initialize(long boardSize, void *privStorage)
{
	unsigned long *p, *q;
	long i, index, numRealSquares;
	char *ptr;
	unsigned long z;
#ifdef USE_HASH
	unsigned long r1, r2;
#endif

	kX      = - boardSize * 5;
	kC      = - boardSize * 4;
	kEdge   = 4;
	kCorner = boardSize * 13;
	
	gRealBoardSize = boardSize + 2;
	gNumOnSquares = boardSize * boardSize;
	numRealSquares = gRealBoardSize * gRealBoardSize;
	
	ptr = privStorage;

	gSquares = (unsigned long *)ptr;
	gOnBoardStart = gSquares + gRealBoardSize + 1;
	gOnBoardEnd   = gSquares + (gRealBoardSize * (gRealBoardSize - 1) - 2);
	ptr += numRealSquares * 4;
	// worst case 66*66*4 = 17,424 bytes (~17K)
	
#ifdef USE_HASH
	gWhiteHashOffset = numRealSquares;
	gBlackHashOffset = numRealSquares * 2;
	gFlipHashOffset  = numRealSquares * 3;
	ptr += (numRealSquares * 3) * 4;
	// worst case 66*66*3*4 = 52,272 bytes (~51K)
	
	gHashTable = (SHash *)ptr;
	ptr += kHashTableSize * sizeof(SHash);
	// 32768*16 = 524,288 bytes (512K)
#endif

	gEmptyAdj = (PSQUARE *)ptr;	
	ptr += gNumOnSquares * 4;
	// worst case 64*64*4 = 16,384 bytes (16K)
	
	gChanges = (unsigned long *)ptr;
	ptr += 65536; // e.g. 64 moves (deep) * 256 longs/move * 4 bytes/long
	// 65,536 bytes (64K)

	gMobility = (long *)ptr;
	ptr += 1024; // e.g. 256 moves deep * 4 bytes/long
	// 1,024 bytes (1K)
	
	gCountZeros = (unsigned long *)ptr;
	ptr += 256 * 4;
	// 256*4 = 1,024 bytes (1K)
	
	gTree = (PSQUARE *)ptr;
	// Gets what's left, almost 400K!


	// ** Calculate directional offsets
	gOffsets[DIR_S]  = gRealBoardSize;
	gOffsets[DIR_N]  = - gRealBoardSize;
	gOffsets[DIR_SE] = gRealBoardSize + 1;
	gOffsets[DIR_NW] = - gRealBoardSize - 1;
	gOffsets[DIR_NE] = - gRealBoardSize + 1;
	gOffsets[DIR_SW] = gRealBoardSize - 1;
	
	// ** Borders
	//   Upper/Lower
	p = gSquares;
	q = gSquares + (gRealBoardSize * (gRealBoardSize - 1));
	i = gRealBoardSize;
	do {
		*(p++) = BORDER_BIT;
		*(q++) = BORDER_BIT;
	} while (--i);
	
	//   Sides
	p = gSquares + gRealBoardSize;
	i = gRealBoardSize - 2;
	do {
		*p = BORDER_BIT;
		p += (gRealBoardSize - 1);
		*(p++) = BORDER_BIT;
	} while (--i);	
	
	// ** Edges
	//   Upper/Lower
	p = gOnBoardStart;
	q = gOnBoardEnd;
	i = boardSize;
	do {
		*(p++) = NW_BORDER | N_BORDER | NE_BORDER;
		*(q--) = SW_BORDER | S_BORDER | SE_BORDER;
	} while (--i);

	//   Sides
	p = gOnBoardStart;
	q = gOnBoardEnd;
	i = boardSize;
	do {
		*p |= NW_BORDER | W_BORDER | SW_BORDER;
		*q |= NE_BORDER | E_BORDER | SE_BORDER;
		p += gRealBoardSize;
		q -= gRealBoardSize;
	} while (--i);
	
	// ** Starting configuration
	// Set up initial disks and adjacent empty squares
	// "On your first move, you should initialize the board
	// with white tiles at (row,col) = (boardSize/2-1,boardSize/2-1) and
	// (boardSize/2,boardSize/2), and black tiles at (boardSize/2-1,boardSize/2)
	// and (boardSize/2,boardSize/2-1)"
	gCounts[WHITE_INDEX] = gCounts[BLACK_INDEX] = 2;

	gSizeEmptyAdj = 12;
	i = boardSize >> 1; // x2
	index = XY2INDEX(i-1, i-1);
	
	p = &gSquares[index];
	*p = SE; gEmptyAdj[0] = p;
	*(++p) = EMPTYADJ_BIT(1) | S | SE; gEmptyAdj[1] = p;
	*(++p) = EMPTYADJ_BIT(2) | SW | S; gEmptyAdj[2] = p;
	*(++p) = EMPTYADJ_BIT(3) | SW; gEmptyAdj[3] = p;
	
	p += gRealBoardSize - 3;
	*p = EMPTYADJ_BIT(4) | E | SE; gEmptyAdj[4] = p;
	*(++p) = WHITE | S | SE | E;
	*(++p) = BLACK | W | SW | S;
	*(++p) = EMPTYADJ_BIT(5) | W | SW; gEmptyAdj[5] = p;
	
	p += gRealBoardSize - 3;
	*p = EMPTYADJ_BIT(6) | E | NE; gEmptyAdj[6] = p;
	*(++p) = BLACK | N | NE | E;
	*(++p) = WHITE | W | NW | N;
	*(++p) = EMPTYADJ_BIT(7) | W | NW; gEmptyAdj[7] = p;
	
	p += gRealBoardSize - 3;
	*p = EMPTYADJ_BIT(8) | NE; gEmptyAdj[8] = p;
	*(++p) = EMPTYADJ_BIT(9) | N | NE; gEmptyAdj[9] = p;
	*(++p) = EMPTYADJ_BIT(10) | NW | N; gEmptyAdj[10] = p;
	*(++p) = EMPTYADJ_BIT(11) | NW; gEmptyAdj[11] = p;

	gNWCorner = gOnBoardStart;
	gNWC1 = gNWCorner + 1; *gNWC1 |= BAD_BIT;
	gNWC2 = gNWCorner + gRealBoardSize; *gNWC2 |= BAD_BIT;
	gNWX = gNWC2 + 1; *gNWX |= BAD_BIT;

	gNECorner = gNWCorner + boardSize - 1;
	gNEC1 = gNECorner - 1; *gNEC1 |= BAD_BIT;
	gNEC2 = gNECorner + gRealBoardSize; *gNEC2 |= BAD_BIT;
	gNEX = gNEC2 - 1; *gNEX |= BAD_BIT;
	
	gSWCorner = gOnBoardEnd - boardSize + 1;
	gSWC1 = gSWCorner + 1; *gSWC1 |= BAD_BIT;
	gSWC2 = gSWCorner - gRealBoardSize; *gSWC2 |= BAD_BIT;
	gSWX = gSWC2 + 1; *gSWX |= BAD_BIT;
	
	gSECorner = gOnBoardEnd;
	gSEC1 = gSECorner - 1; *gSEC1 |= BAD_BIT;
	gSEC2 = gSECorner - gRealBoardSize; *gSEC2 |= BAD_BIT;
	gSEX = gSEC2 - 1; *gSEX |= BAD_BIT;
	
	// Precalculate gCountZeros
	// (Could have had the compiler fill these in, but I'm not
	//  THAT desperate for speed)
	for (z=0; z<256; ++z) {
		gCountZeros[z] =
			8 - (z & 1) - ((z>>1) & 1) - ((z>>2) & 1) - ((z>>3) & 1) -
			((z>>4) & 1) - ((z>>5) & 1) - ((z>>6) & 1) - ((z>>7) & 1);
	}

#ifdef USE_HASH
	gHashValue = 0xFFFFFFFF;
	
	// Initialize gHashKeys
	srand(0x1234); //srand(time(NULL));	
	for (i=0; i<numRealSquares; ++i) {
		r1 = rand() + ((unsigned long)rand() << 16);
		r2 = rand() + ((unsigned long)rand() << 16);
		gSquares[gWhiteHashOffset + i] = r1;
		gSquares[gBlackHashOffset + i] = r2;
		gSquares[gFlipHashOffset  + i] = r1 ^ r2;
	}

	// Clear gHashTable
	{
		SHash *pHashTable = gHashTable;
		
		i = kHashTableSize - 1;
		do {
			pHashTable->HashValue = 0;
			pHashTable->Depth = -100;
			pHashTable->BestMove = NULL;
			pHashTable->Type = INVALID;
			pHashTable->Score = 0;
			++pHashTable;
		} while (i--);
	}
#endif
	
	gIncScore = 0;
}
