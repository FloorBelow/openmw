#ifndef OPENMW_ESM_CELLREF_H
#define OPENMW_ESM_CELLREF_H

#include <limits>
#include <string>

#include "components/esm/defs.hpp"
#include "components/esm/esmcommon.hpp"
#include "components/esm/refid.hpp"

namespace ESM
{
    class ESMWriter;
    class ESMReader;

    const int UnbreakableLock = std::numeric_limits<int>::max();

    struct RefNum
    {
        unsigned int mIndex = 0;
        int mContentFile = -1;

        void load(ESMReader& esm, bool wide = false, NAME tag = "FRMR");

        void save(ESMWriter& esm, bool wide = false, NAME tag = "FRMR") const;

        inline bool hasContentFile() const { return mContentFile >= 0; }

        inline bool isSet() const { return mIndex != 0 || mContentFile != -1; }
    };

    /* Cell reference. This represents ONE object (of many) inside the
    cell. The cell references are not loaded as part of the normal
    loading process, but are rather loaded later on demand when we are
    setting up a specific cell.
    */

    class CellRef
    {
    public:
        // Reference number
        // Note: Currently unused for items in containers
        RefNum mRefNum;

        ESM::RefId mRefID; // ID of object being referenced

        float mScale; // Scale applied to mesh

        // The NPC that owns this object (and will get angry if you steal it)
        ESM::RefId mOwner;

        // Name of a global variable. If the global variable is set to '1', using the object is temporarily allowed
        // even if it has an Owner field.
        // Used by bed rent scripts to allow the player to use the bed for the duration of the rent.
        std::string mGlobalVariable;

        // ID of creature trapped in this soul gem
        ESM::RefId mSoul;

        // The faction that owns this object (and will get angry if
        // you take it and are not a faction member)
        ESM::RefId mFaction;

        // PC faction rank required to use the item. Sometimes is -1, which means "any rank".
        int mFactionRank;

        // For weapon or armor, this is the remaining item health.
        // For tools (lockpicks, probes, repair hammer) it is the remaining uses.
        // For lights it is remaining time.
        // This could be -1 if the charge was not touched yet (i.e. full).
        union
        {
            int mChargeInt; // Used by everything except lights
            float mChargeFloat; // Used only by lights
        };
        float mChargeIntRemainder; // Stores amount of charge not subtracted from mChargeInt

        // Remaining enchantment charge. This could be -1 if the charge was not touched yet (i.e. full).
        float mEnchantmentCharge;

        // This is 5 for Gold_005 references, 100 for Gold_100 and so on.
        int mGoldValue;

        // For doors - true if this door teleports to somewhere else, false
        // if it should open through animation.
        bool mTeleport;

        // Teleport location for the door, if this is a teleporting door.
        Position mDoorDest;

        // Destination cell for doors (optional)
        ESM::RefId mDestCell;

        // Lock level for doors and containers
        int mLockLevel;
        ESM::RefId mKey, mTrap; // Key and trap ID names, if any

        // This corresponds to the "Reference Blocked" checkbox in the construction set,
        // which prevents editing that reference.
        // -1 is not blocked, otherwise it is blocked.
        signed char mReferenceBlocked;

        // Position and rotation of this object within the cell
        Position mPos;

        /// Calls loadId and loadData
        void load(ESMReader& esm, bool& isDeleted, bool wideRefNum = false);

        void loadId(ESMReader& esm, bool wideRefNum = false);

        /// Implicitly called by load
        void loadData(ESMReader& esm, bool& isDeleted);

        void save(ESMWriter& esm, bool wideRefNum = false, bool inInventory = false, bool isDeleted = false) const;

        void blank();
    };

    void skipLoadCellRef(ESMReader& esm, bool wideRefNum = false);

    inline bool operator==(const RefNum& left, const RefNum& right)
    {
        return left.mIndex == right.mIndex && left.mContentFile == right.mContentFile;
    }

    inline bool operator<(const RefNum& left, const RefNum& right)
    {
        if (left.mIndex < right.mIndex)
            return true;
        if (left.mIndex > right.mIndex)
            return false;
        return left.mContentFile < right.mContentFile;
    }

}

namespace std
{

    // Needed to use ESM::RefNum as a key in std::unordered_map
    template <>
    struct hash<ESM::RefNum>
    {
        size_t operator()(const ESM::RefNum& refNum) const
        {
            assert(sizeof(ESM::RefNum) == sizeof(size_t));
            size_t s;
            memcpy(&s, &refNum, sizeof(size_t));
            return hash<size_t>()(s);
        }
    };

}

#endif
