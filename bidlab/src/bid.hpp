#ifndef _BID_HPP_
#define _BID_HPP_

typedef int bid;
typedef char bidName[3];
enum vulnerability  { NoneVul, NSVul, EWVul, BothVul };

static const bid bidPass = 4;
static const bid bidInvalid = -1;
static const bid bidNotFound = 3;
static const int bidNumStrains = 5;
static const int bidClub = 0;
static const int bidDiamond = 1;
static const int bidHeart = 2;
static const int bidSpade = 3;
static const int bidNT = 4;
static const int bidNumLevels = 7;
static const bid bidMaxBid = bidPass + bidNumStrains * bidNumLevels;

inline bid makeBid (int level, int strain)  { return level * bidNumStrains + strain; }
inline int bidLevel (bid b)                 { return b / bidNumStrains; }
inline int bidStrain (bid b)                { return b % bidNumStrains; }
inline bool bidIsPass (bid b)               { return (b == bidPass); }
inline bool bidValid (bid b)                { return (b >= bidPass); }

#endif // _BID_HPP_
