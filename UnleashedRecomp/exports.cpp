#include <api/SWA.h>
#include <app.h>
#include <apu/embedded_player.h>
#include <cpu/guest_stack_var.h>
#include <kernel/function.h>
#include <kernel/heap.h>
#include <kernel/memory.h>
#include <ui/game_window.h>
#include <patches/inspire_patches.h>

void Game_PlaySound(const char* pName)
{
    if (EmbeddedPlayer::s_isActive)
    {
        EmbeddedPlayer::Play(pName);
    }
    else
    {
        if (!App::s_isInit || g_memory.base == nullptr || GetPPCContext() == nullptr)
            return;

        // Use EVENT category in cutscenes since SYSTEM gets muted by the game.
        uint32_t category = !InspirePatches::s_sceneName.empty() ? 10 : 7;

        auto gameDocument = (be<uint32_t>*)g_memory.Translate(0x83367900);
        if (gameDocument->get() == 0)
            return;

        guest_stack_var<boost::anonymous_shared_ptr> soundPlayer;
        GuestToHostFunction<void>(sub_82B4DF50, soundPlayer.get(), gameDocument->get(), category, 0, 0);

        auto soundPlayerObject = soundPlayer->get();
        if (soundPlayerObject == nullptr)
            return;

        uint32_t soundPlayerVtableGuest = *(be<uint32_t>*)soundPlayerObject;
        if (soundPlayerVtableGuest == 0)
            return;

        auto soundPlayerVtable = (be<uint32_t>*)g_memory.Translate(soundPlayerVtableGuest);
        uint32_t virtualFunction = *(soundPlayerVtable + 1);
        if (virtualFunction == 0)
            return;

        size_t strLen = strlen(pName);
        void *strAllocation = g_userHeap.Alloc(strLen + 1);
        if (strAllocation == nullptr)
            return;

        memcpy(strAllocation, pName, strLen + 1);
        GuestToHostFunction<void>(virtualFunction, soundPlayer->get(), strAllocation, 0);
        g_userHeap.Free(strAllocation);
    }
}
