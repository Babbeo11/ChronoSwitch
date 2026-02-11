# ChronoSwitch | Unreal Engine 5.7

**ChronoSwitch** is a first-person multiplayer puzzle-platformer. The core mechanic involves navigating two parallel timelines (Past and Future) where the player's actions in one era dictate the state of the other through physics-based causality.

> **Status:** In Development (Academic Project - ITS Video Game Specialist)
> **Role:** Lead Programmer & Game Director
> **Tech Stack:** C++, UE 5.7, Networking (RPCs), Character Movement Component (CMC), Enhanced Input.

---

## 🛠 Technical Architecture

### 1. Timeline Switching & Collision Masking
To maximize performance, the world remains static while the **Character** handles the transition logic. 
- **Character-Centric Logic:** Instead of toggling world actors, the Character capsule updates its collision responses to specific Object Channels (`ECC_Past` / `ECC_Future`) in an $O(1)$ operation. This allows for seamless traversal of era-specific obstacles without heavy CPU overhead.
- **Visual State:** A Material Parameter Collection (MPC) is updated via C++ to shift the global color palette (Blue for Past, Orange for Future) and drive environment shaders in real-time.

<br>
<div align="center">
  <img src="https://github.com/user-attachments/assets/8f649451-8333-4866-9d2e-c0242aea5f93" width="100%" />
  <p><i><b>Fig 1. O(1) Timeline Switching:</b> The character capsule updates its collision response to custom Object Channels (ECC_Past/Future) instantly, allowing seamless traversal without the overhead of toggling world actors.</i></p>
</div>
<br>

### 2. Causal Actor System (Physics Synchronization)
The `CausalActor` (inheriting from `TimelineBaseActor`) manages objects that exist across both timelines simultaneously.
- **State Management:** Uses an `Enum` to define visibility and collision. In the `Both_Causal` state, the object is physically active in both eras.
- **Transform Tracking:** Custom C++ logic ensures the "Future" mesh constantly aligns with the "Past" mesh's transform. 
- **Physics Correction:** If pushed by physics, it uses corrective impulses to maintain sync. If "grabbed" by the player, it performs direct transform updates to prevent desynchronization between timelines.

<br>
<div align="center">
  <img src="https://github.com/user-attachments/assets/16fb58b9-7914-48a6-8133-761c76f3d1b7" width="100%" />
  <p><i><b>Fig 2. Causal Physics Sync:</b> The "Future" mesh actively tracks the authoritative "Past" transform using corrective impulses and interpolation, ensuring synchronization across timelines during physics interactions.</i></p>
</div>
<br>


### 3. Networking & Movement Correction
The switch mechanic is replicated to handle both player-initiated actions and server-forced events (e.g., global timers).
- **Client-Side Prediction:** The client updates local collisions immediately upon input for zero-latency movement.
- **Network Correction (Rubber-banding Prevention):** When the server confirms a timeline change, `FlushServerMoves()` is utilized to clear the movement buffer. This prevents rubber-banding artifacts caused by outdated collision data in the predicted moves.
- **State Validation:** The system verifies the `PlayerState` to avoid redundant cosmetic triggers if the local prediction already matches the server-authoritative state.


---

## 💻 Technical Snippets

### 1. Network Correction & Prediction Validation
*This snippet handles server-dictated timeline changes and prevents movement rubber-banding by flushing the CMC buffer.*

```cpp
void AChronoSwitchCharacter::Client_ForcedTimelineChange_Implementation(uint8 NewTimelineID)
{
    // 1. Network Correction: Flush server moves to prevent rubber-banding.
    // Outdated predicted moves rely on old collision data; flushing ensures 
    // the movement buffer is cleared for the new timeline state.
    if (UCharacterMovementComponent* CMC = GetCharacterMovement())
    {
        CMC->FlushServerMoves();
    }

    // 2. Prediction Check: Verify if the local state already matches the server's update.
    if (const AChronoSwitchPlayerState* PS = GetPlayerState<AChronoSwitchPlayerState>())
    {
        // If IDs match, local prediction was successful; skip to avoid double cosmetics.
        if (PS->GetTimelineID() == NewTimelineID) return;
    }

    // 3. Forced Update: Execution for unpredicted changes (e.g., Global Timer events).
    HandleTimelineUpdate(NewTimelineID);
}
```
### 2. O(1) Collision Masking Logic
*Instead of toggling world actors, the character updates its collision response to specific Era Channels, allowing for instant and optimized transitions.*

```cpp
void AChronoSwitchCharacter::HandleTimelineUpdate(uint8 NewTimelineID)
{
    LocalTimelineID = NewTimelineID;

    // Toggle responses: Block current era, Ignore the other.
    ECollisionResponse PastResponse   = (NewTimelineID == 0) ? ECR_Block : ECR_Ignore;
    ECollisionResponse FutureResponse = (NewTimelineID == 1) ? ECR_Block : ECR_Ignore;

    if (UCapsuleComponent* Capsule = GetCapsuleComponent())
    {
        // ECC_PastTimeline and ECC_FutureTimeline are custom Object Channels
        Capsule->SetCollisionResponseToChannel(ECC_PastTimeline, PastResponse);
        Capsule->SetCollisionResponseToChannel(ECC_FutureTimeline, FutureResponse);
    }

    // Trigger visual feedback (MPC) and UI updates
    OnTimelineStateChanged(NewTimelineID);
}
```
## 📂 Project Structure
- **/Source**: Contains all core C++ classes (Character, PlayerState, TimelineBaseActor, CausalActor). This is where the primary logic and networking systems are implemented.
- **/Config**: Custom Collision Channel definitions (`DefaultEngine.ini`) and Input Mappings.
- **/Content**: Visual assets, Blueprints (extending C++ classes), Level Design, and Material Parameter Collections.



---

## 🚀 Setup & Installation
1. **Clone the repository** to your local machine.
2. **Right-click the `.uproject` file** and select "Generate Visual Studio project files".
3. **Open the `.sln` file** and compile the project in **Development Editor** mode.
4. **Launch the project** in Unreal Engine 5.7.

---
*Note: This is an Academic Project developed for the ITS Video Game Specialist course. Final release: April 2026.*
