#ifndef OPENMW_ESM_PROJECTILESTATE_H
#define OPENMW_ESM_PROJECTILESTATE_H

#include <string>

#include <osg/Quat>
#include <osg/Vec3f>

#include "effectlist.hpp"

#include "components/esm/refid.hpp"
#include "components/esm/util.hpp"

namespace ESM
{

    // format 0, savegames only

    struct BaseProjectileState
    {
        RefId mId;

        Vector3 mPosition;
        Quaternion mOrientation;

        int mActorId;

        void load(ESMReader& esm);
        void save(ESMWriter& esm) const;
    };

    struct MagicBoltState : public BaseProjectileState
    {
        RefId mSpellId;
        float mSpeed;
        int mSlot;

        void load(ESMReader& esm);
        void save(ESMWriter& esm) const;
    };

    struct ProjectileState : public BaseProjectileState
    {
        RefId mBowId;
        Vector3 mVelocity;
        float mAttackStrength;

        void load(ESMReader& esm);
        void save(ESMWriter& esm) const;
    };

}

#endif
