#define OGSR_PHYSX2_STATIC 1
#include "physics_physx/physx_ragdoll_c_v2.h"

// Include v1 after v2 to prove the forward declarations and frozen payload
// types are compatible in either include order.
#include "physics_physx/physx_ragdoll_c.h"

#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout_v<OgsrPhysX2SceneHandle>);
static_assert(std::is_standard_layout_v<OgsrPhysX2RagdollHandle>);
static_assert(std::is_standard_layout_v<OgsrPhysX2ApiInfo>);
static_assert(std::is_standard_layout_v<OgsrPhysX2SceneConfig>);
static_assert(std::is_standard_layout_v<OgsrPhysX2RagdollInstanceConfig>);
static_assert(std::is_standard_layout_v<OgsrPhysX2StepResult>);

static_assert(std::is_trivially_copyable_v<OgsrPhysX2SceneHandle>);
static_assert(std::is_trivially_copyable_v<OgsrPhysX2RagdollHandle>);
static_assert(std::is_trivially_copyable_v<OgsrPhysX2ApiInfo>);
static_assert(std::is_trivially_copyable_v<OgsrPhysX2SceneConfig>);
static_assert(std::is_trivially_copyable_v<OgsrPhysX2RagdollInstanceConfig>);
static_assert(std::is_trivially_copyable_v<OgsrPhysX2StepResult>);

static_assert(sizeof(OgsrPhysX2SceneHandle) == 8);
static_assert(sizeof(OgsrPhysX2RagdollHandle) == 16);
static_assert(sizeof(OgsrPhysX2ApiInfo) == 32);
static_assert(sizeof(OgsrPhysX2SceneConfig) == 48);
static_assert(sizeof(OgsrPhysX2RagdollInstanceConfig) == 32);
static_assert(sizeof(OgsrPhysX2StepResult) == 40);

static_assert(offsetof(OgsrPhysX2RagdollHandle, instanceGeneration) == 12);
static_assert(offsetof(OgsrPhysX2StepResult, stepIndex) == 16);
static_assert(offsetof(OgsrPhysX2StepResult, frameId) == 24);

int main()
{
    OgsrPhysX2SceneHandle scene{};
    OgsrPhysX2RagdollHandle ragdoll{};
    OgsrPhysX2ApiInfo info{};
    return (scene.generation == 0 && ragdoll.instanceGeneration == 0 && info.structSize == 0) ? 0 : 1;
}
