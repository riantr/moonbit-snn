# Changelog — snn_mbt

This file is the historical record of what shipped in which release. It was
carried inside `README.md` until this release, where it was split out so the
package page could describe the *current* release instead of the last twenty
batches of development notes.

**The counts in this file are historical.** They are the numbers as they were
when the release was made, and they are deliberately not corrected: a
changelog records what was true then. The current counts — the test count, the
package count, the source-file count — live in `README.md`, and they are pinned
in `verify/tree_baseline.json` and `verify/test_baseline.json` and checked by
the `docs` gate on every push.

Sections that describe a state that has since been replaced are marked
**Superseded** in place.

---

## Recent additions (v0.44–v0.47, 2026-10-01)

### v0.44.0 — Prioritized Experience Replay (PER, Schaul 2016)

- `mbt/sum_tree.mbt` — segmented priority tree with O(log N) sample/update. API: `SumTree::new(capacity)`, `add(priority) -> tree_idx`, `set_at(slot, p)`, `find_prefix(s) -> (tree_idx, p)`, `find_prefix_batch(segs)`, `total()`, `len()`, `get_leaf(slot)`.
- `mbt/prioritized_replay.mbt` — `PrioritizedReplayBuffer` with proportional priorities `P(i) = p_i^α / Σ p_j^α`, importance-sampling weights `w_i = (N·P(i))^{-β} / max_j w_j` normalised to 1.0, FIFO wrap-around, max-priority insertion trick (so every transition is sampleable at least once). API: `new(capacity, α, β, ε)`, `push(s, a, r, s', done)`, `sample(batch_size, rng) -> (states, actions, rewards, next_states, dones, slot_indices, is_weights, probs)`, `update_priorities(slot_indices, td_errors)`, `len()`, `total_priority()`, `max_priority()`.
- 22 new tests (`sum_tree_test.mbt`, `prioritized_replay_test.mbt`).

### v0.45.0 — N-step returns (R2D2-style)

- `mbt/n_step_buffer.mbt` — FIFO `StepRecord` ring. `NStepBuffer::new(n)`, `push(s, a, r, s', done) -> Bool` (true when window ready), `at(i)` chronological, `reset()`, `n_step_return(root, γ) -> (G_n, h_eff, s_root, s_look, truncated)` with terminal-truncation and partial-window handling.
- `mbt/dqn_n_step.mbt` — `dqn_update_step_n_step` (γ^h bootstrap + no-bootstrap when terminal), `train_dqn_n_step` (NStepBuffer → ReplayBuffer flush + partial-window flush at episode end).
- 14 new tests.

### v0.46.0 — Distributional SAC (DSAC, Ma et al. 2021)

- `mbt/dsac.mbt` — `LinearGaussianQNet` with μ + log-σ heads per action (σ = exp(log-σ) ∈ [1e-3, 10]), Gaussian NLL helper, `DSac` agent (policy + twin Gaussian critics + twin targets + α), `dsac_critic_update` with analytic ∂NLL/∂μ = (μ−t)/σ² and ∂NLL/∂log-σ = 0.5(z²−1), `dsac_soft_update` Polyak averaging on all 4 weight arrays, `dsac_soft_target` = SAC soft-Bellman + ε·N(0, 0.01) stochastic perturbation, `dsac_sample_action`.
- 12 new tests.

### v0.47.0 — Batch 4 unified Network Simulator framework

- `mbt/network_simulator.mbt` — extends the existing `HeterogeneousModel` framework (compose.mbt). New helpers: `NetworkSummary` struct (counts of pops/conns/stims/monitors/stdp/stp entries + total_neurons + current_time), `network_summary(model)`, `reset_heterogeneous_full` (time → 0 + monitor.clear_records + per-type STDP/STP entry resets), `merge_heterogeneous(a, b)` (concatenates two HeterogeneousModels via compose), `heterogeneous_sim_for_with_log(model, duration, log_every_ms) -> Array[SimLog]` (periodic SimLog snapshots with `t`, `active_monitors`, `total_spikes`), `step_heterogeneous_with_record(model, dt, callback)` (caller-supplied callback after each step).
- 9 new tests.

## Recent additions (v0.48–v0.53, 2026-10-01)

### v0.48.0 — Bit-exact Julia parity framework

- `mbt/julia_parity.mbt` — ULP-distance comparator (`float32_ulp_distance` via `Float::reinterpret_as_int`), `float32_close(a, b, tol_ulps)`, `compare_float_traces` (element-by-element with max_abs + max_ulp), `compare_spike_trains` (greedy nearest-neighbour matching within `tol_steps`), `parse_parity_csv` (handles int/float traces + comments + blanks), int/float string parsers.
- `mbt/julia_parity_test.mbt` — 12 tests including closed-form parity for a single IF neuron with constant current (geometric series `(1-α)^n · v₀ + (1−(1-α)^n) · v_eq`). The FP accumulation mismatch between MoonBit's `step_neuron` and the analytical closed-form is documented as a known limitation; tolerances (0.05–0.1 abs + 4096–8192 ULPs) catch regressions without requiring bit-exact analytical agreement.
- **First piece of the bit-exact Julia parity infrastructure** that unblocks P0-1 (README TODO "Bit-exactness verification vs Julia").

### v0.49.0 — IF_CANaHP example + AnyPop wiring

- `mbt/population.mbt` — adds `IFCANAHP_(IFCANAHP)` to `AnyPop` enum + dispatch in `integrate_any` (calls `integrate_ifcanahp`) and `any_n_neurons`.
- `mbt/network_simulator.mbt` — adds `IFCANAHP_` case to `count_neurons`.
- `mbt/population_test.mbt` — adds `IFCANAHP_(_)` catch-all arm.
- `mbt/examples/if_canahp/main.mbt` (NEW) + `moon.pkg` (NEW) — 100 IF_CANAHP neurons (Brogdon 2022 CAN-AHP LIF) with constant I=50 driven through `heterogeneous_sim_for` for 100 ms.
- `mbt/neuron_if_canahp_test.mbt` — 2 new tests (`ifcanahp_example_smoke` 100-neuron finite-state, `ifcanahp_example_stable_dt` smaller dt bounds v).
- The empty `if_canahp/` directory was the missing example for the already-ported neuron.

### v0.50.0 — Multipod::step bug fix + re-enabled runtime tests

- Fixed **1-based vs 0-based indexing bug** in the per-receptor `h_d` update block (`h_d[d][i][4]` was written but storage is sized 4 → indices 0..3). The receptor marker arrays stay as Julia's 1-based labels `[1, 2]` / `[3, 4]`; only the storage indices are corrected to 0-based.
- Re-enabled 4 deferred runtime tests in `neuron_multipod_test.mbt` (stubbed since v0.10.53 due to "stack overflow"). The actual root cause was the indexing bug, not JIT stack depth.
- New `multipod_runtime_test.mbt` with 3 smoke tests verifying long-simulation `Multipod::step` does NOT panic.
- Heun corrector still TODO per README.

### v0.51.0 — izhikevich_balanced_postspike.jl complete

- Extended `mbt/examples/izhikevich_balanced_postspike/main.mbt` from E-only (100 RS IZ + Bernoulli + refractory) to a true E/I balanced network: 100 RS IZ (excitatory) + 25 FS IZ (inhibitory), both with `tabs_const=4` refractory, both receive 1 kHz Bernoulli drive; I→E cross-talk via `gi` spikes when an I neuron fires.
- `mbt/izhikevich_balanced_postspike_test.mbt` — new "E/I balanced" test verifying both populations fire under drive.

### v0.52.0 — tripod_network.jl example + tests

- `mbt/tripod_network_test.mbt` (NEW) — 3 integration tests mirroring the existing `examples/tripod_network/main.mbt` scenario (E Tripod + I1/I2 IF + CompartmentSynapses I1→E[:soma, iSTDPRate] + I2→E[:d1, iSTDPPotential]): smoke run, I-populations-active-under-tonic-drive, iSTDP-rate-step-modifies-weights.
- The example itself was already complete with the manual sim loop + iSTDP dispatch; only test coverage was missing.

### v0.53.0 — tripod_current.jl example + tests

- `mbt/tripod_current_test.mbt` (NEW) — 3 integration tests mirroring the existing `examples/tripod_current/main.mbt` scenario (4 TripodHet + PoissonLayerStimulusTripod exc@20Hz/inh@3Hz on :d1): smoke run, produces-bounded-spike-count, d1-receives-exc-and-inh.
- The example itself was already complete; only test coverage was missing.

## Recent additions (v0.54–v0.56, 2026-10-01) — Batch B (continuous-control RL + serialization)

### v0.54.0 — DDPG (Deep Deterministic Policy Gradient, Lillicrap 2015)

- `mbt/ddpg.mbt` (NEW) — `DeterministicPolicy` (state → continuous action via tanh-squash to `[action_low, action_high]`) + `QNetworkContinuous` ([state, action] → scalar Q with one ReLU hidden layer) + `DDPG` agent struct (deterministic actor + twin critics + twin target critics + target actor + 5 Polyak-averaged target nets + γ/τ/exploration_noise).
- `mbt/ddpg_update.mbt` (NEW) — `ContinuousReplayBuffer` (FIFO wrap-around with `(s, a, r, s', done)` transitions) + `ddpg_update_critic` (analytic twin-critic TD gradient update) + `ddpg_update_actor` (closed-form critic gradient through `sign(ReLU_out)` gate, avoids backward pass through actor ReLU).
- `mbt/ddpg_test.mbt` (NEW) — 15 tests: `ddpg_select_action_shape`, `ddpg_select_action_no_grad_shape`, `ddpg_update_critic_runs_without_crash` (NaN guard), `ddpg_update_actor_modifies_w2` (mutates w2 in place), `ddpg_twin_critic_does_not_collapse_on_seeded_inputs`, `ddpg_soft_update_polyak`, `continuous_replay_buffer_push_and_len`, `continuous_replay_buffer_overflow_wraps`, `deterministic_policy_tanh_squash_clips`, `deterministic_policy_action_range`, `deterministic_policy_random_init`, `qnet_continuous_forward_shape`, `qnet_continuous_zero_input_zero_weight`, `ddpg_train_smoke_pendulum_like_env` (end-to-end 4-step smoke train), `ddpg_select_action_is_deterministic`.

### v0.55.0 — TD3 (Twin Delayed DDPG, Fujimoto 2018)

- `mbt/td3.mbt` (NEW) — `TD3Agent` with actor + twin Q-critics (q1/q2) + twin target Q-critics + target actor + 5 Polyak-averaged target nets + Adam optimizers. Adds `policy_delay=2`, `target_noise_std=0.2`, `target_noise_clip=0.5` to the DDPG base.
- `td3_smoothed_target_action(action, noise_std, clip_range, rng)` — `clip(action + clip(standard_normal, -c, c), -clip_range, clip_range)` where `c = noise_std`, applied to the target-actor action in the twin-critic TD target.
- `td3_actor_update(actor, critic1, critic2, target_q1, target_q2, batch, lr)` — extracted from `ddpg_update_actor` (same body, separate name for dispatching clarity); closed-form critic gradient with sign(ReLU_out) gate.
- `td3_soft_update(target_net, source_net, tau)` — Polyak averaging over all 5 networks (actor, q1, q2, target_actor, target_q1, target_q2).
- `td3_update_step(agent, batch, lr_actor, lr_critic)` — twin-critic TD target `y = r + γ·min(q1', q2')`, critic update every step, actor + target-net soft update only when `step % policy_delay == 0`.
- `td3_select_action(state, actor, exploration_noise_std, action_max, rng)` — action + Gaussian noise clipped to `[-action_max, action_max]`.
- `train_td3(agent, env_step, n_episodes, max_steps, ...) -> Array[Float]` — closure-based env_step callback `(state, action) -> (next_state, reward, done)`, returns per-episode total rewards.
- 8 tests: `td3_select_action_shape`, `td3_select_action_no_grad_shape`, `td3_smoothed_target_action_clip`, `td3_smoothed_target_action_noise`, `td3_soft_update_polyak`, `td3_update_step_twin_critic_td_target`, `td3_update_step_delayed_actor_update_skips_when_count_lt_delay` (verifies the skip path), `td3_train_smoke_pendulum_like_env`.

### v0.56.0 — Model + optimizer checkpoint serialization

- `mbt/serialize.mbt` (NEW) — pure-functional String round-trip for DQN / DDPG / TD3 model parameters + Adam optimizer state. Pure-functional API (no file I/O coupling; caller handles persistence via MoonBit `@fs` or piping to `moon run`).
- Lightweight `SNNCKPT v1` header format with key=value lines, no nested objects. Each model is one block; combined checkpoint inlines model + optimizer sub-blocks via escape sequences (`\n` → `\\n`, `\` → `\\`).
- 1D arrays encoded as `[v0;v1;...]` (semicolon-separated); 2D arrays as `[r0c0;r0c1|r1c0;r1c1;...]` (semicolons inside rows, pipes between rows). Both separators are non-numeric and never appear in `Float::to_string` output, so the format is safe to embed inside the `key=value\n` line-oriented header.
- **Float encoding is bit-exact** via `Float::reinterpret_as_int` + `Float::reinterpret_from_int` (NOT `Float::to_string` / `parse_float` — initial attempt failed 4 tests on ULP drift because MoonBit 0.1.20260920's default Float printer truncates to ~6 digits, which loses last-bit precision for values like `0.1F`, `0.01F`, `-3.14159F`). Single-Float fields (`QNetworkContinuous.b2`, `action_low`, `action_high`) also use the same bit-exact Int encoding.
- Public API: `serialize_linear_qnet` / `deserialize_linear_qnet`, `serialize_qnetwork_continuous` / `deserialize_qnetwork_continuous`, `serialize_deterministic_policy` / `deserialize_deterministic_policy`, `serialize_adam_state` / `deserialize_adam_state`, `serialize_qnet_checkpoint` / `deserialize_qnet_checkpoint` (returns `(LinearQNet, AdamState, Int)` tuple).
- Deep-copy invariant: the deserialized model has freshly allocated arrays, so subsequent mutation of either side is independent (verified in `serialize_linear_qnet_independent_copy` + `serialize_qnet_checkpoint_independent_copies`).
- 12 tests: round-trip for each model + AdamState (zero state + post-update state with step > 0) + combined checkpoint + header-tag presence + action range preservation + negative-float preservation. `b2` mutation tests skipped because `QNetworkContinuous.b2` is `mut b2` and read-only from outside the defining module in blackbox tests; the default-value round-trip still covers the encode/decode path.
- `AdamState` fields are not `pub`, so tests use `adam_init` / `adam_update_arrays` / `adam_next_step` (the public API) to construct states with step > 0, and `let opt1 = decode_adam_state(s)` to get the deserialized state without mutating it.

## Recent additions (v0.57–v0.60, 2026-10-01) — Batch C (recurrent / POMDP RL)

### v0.57.0 — SequenceReplayBuffer for recurrent RL

- `mbt/sequence_replay_buffer.mbt` (NEW) — per-step continuous replay buffer with T-step rollout sampling for LSTM/GRU actor-critic RL. Stores `(state, action, reward, next_state, done)` in flat row-major Float arrays.
- `sample_seq_batch(batch_size, seq_len, rng)` returns 7 parallel arrays: `obs_seq [batch × seq_len × state_dim]`, `action_seq [batch × seq_len × action_dim]`, `reward_seq [batch × seq_len]`, `next_obs_seq [batch × seq_len × state_dim]`, `done_seq [batch × seq_len]`, `terminal_mask [batch × seq_len]` (1.0F at slots following a `done` boundary so caller can break BPTT), `hidden_init [batch]` (zeros — caller supplies hidden dim).
- Padding behaviour: window crossing a `done` boundary zero-fills the rest and sets `terminal_mask`. Empty buffer (size < seq_len) returns all-zero windows with `terminal_mask` set everywhere (no panic).
- 9 tests: new/len, push, overflow wrap, reset, sample shape, empty-buffer zero-pad, window-crosses-done terminal mask, reproducible under seed, dones propagate in window.

### v0.58.0 — GRUDeterministicPolicy (recurrent actor for POMDPs)

- `mbt/gru_deterministic_policy.mbt` (NEW) — recurrent deterministic actor for partially-observable continuous control. Architecture: `MLP w1 (state→hidden) → ReLU → GRU cell (over time) → MLP w2 (hidden→action) → tanh squash to [action_low, action_high]`. GRU hidden dim = MLP hidden dim so the ReLU-projected state and the GRU input/output match.
- `GRUDeterministicPolicy::new(state_dim, action_dim, gru_hidden, action_low, action_high, seed)` — Xavier-normal init for MLPs (He-style `sqrtf(2/fan_in)` for ReLU), reuses `GruCellParam::new(gru_hidden, gru_hidden, seed+4UL)` for the recurrent cell, zero biases.
- `gru_deterministic_policy_step(policy, obs, hidden) → (action, hidden_next)` — single-step inference. `gru_deterministic_policy_seq_forward(policy, obs_seq, seq_len, hidden_init) → (action_seq, final_hidden)` — flat row-major `[seq_len × action_dim]` output.
- 8 tests: constructor shape, action in squash range, deterministic under seed, different seeds differ, hidden state evolves, seq_forward shape, seq_forward matches single-step chain, deterministic under seed at seq level.

### v0.59.0 — GRUQNetworkContinuous (recurrent critic for POMDPs)

- `mbt/gru_qnetwork_continuous.mbt` (NEW) — recurrent continuous-action Q-network for partially-observable environments. Architecture: `MLP w1 (concat(state, action)→hidden) → ReLU → GRU cell → MLP w2 (hidden→scalar Q)`.
- `GRUQNetworkContinuous::new(state_dim, action_dim, hidden, seed)` — Xavier-normal init for MLPs, GRU via `GruCellParam::new(hidden, hidden, seed+4UL)`. `mlp_w2` is 1×hidden (scalar Q output) + scalar `mut mlp_b2 : Float` bias.
- `gru_qnetwork_continuous_step(qnet, state, action, hidden) → (q, hidden_next)` — single-step inference (returns Float, not array). `gru_qnetwork_continuous_seq_forward(qnet, state_seq, action_seq, seq_len, hidden_init) → (q_seq, final_hidden)` — flat row-major `[seq_len]` output.
- 8 tests: shape, deterministic under seed, different seeds differ, hidden state evolves, zero-zero-zero → 0.0F, seq_forward shape + deterministic, seq_forward matches single-step chain, scalar return type.

### v0.60.0 — DDPG_GRU agent (recurrent actor + twin recurrent critics)

- `mbt/gru_ddpg.mbt` (NEW) — DDPG agent with `GRUDeterministicPolicy` actor + twin `GRUQNetworkContinuous` critics + 5 Polyak-averaged target networks (`actor_target`, `critic1_target`, `critic2_target`). Recurrent hidden state threaded through the twin-critic TD update.
- `DDPG_GRU::new(state_dim, action_dim, hidden, action_low, action_high, gamma, tau, exploration_noise, seed)` — each of the 6 networks gets a distinct seed (sources at seed/seed+1/seed+2, targets at seed+100/seed+101/seed+102) so Polyak-averaged targets start far from sources.
- `ddpg_gru_select_action(agent, obs, hidden, noise_std, rng)` — single-step inference with optional Gaussian exploration noise clipped to `[action_low, action_high]`. `noise_std=0` recovers the deterministic policy output.
- `ddpg_gru_compute_td_target_seq(agent, next_obs_seq, next_act_seq, reward_seq, done_seq, seq_len)` — twin-critic TD target forward: `y_t = r_t + γ·(1 - done_t)·min(q1_target(next), q2_target(next))`. **No parameter update yet** — BPTT-driven update (matvec_backward over T steps + gru_cell_backward over T steps + sign(ReLU_out) gate) is deferred to a follow-up because it's a significant incremental body of work.
- `gru_deterministic_policy_soft_update(target, source, tau)` + `gru_qnetwork_continuous_soft_update(target, source, tau)` — Polyak averaging that updates the MLP weights + biases AND the GRU sub-parameters (`w_z`, `w_r`, `w_n`, `b_z`, `b_r`, `b_n`). `ddpg_gru_soft_update(agent)` calls all three.
- `pub(all) struct DDPG_GRU` with `mut tau : Float` — required for cross-file record-update `{ ..agent, tau: 1.0F }` (MoonBit's blackbox test rule: `mut` field is only mutable if the containing struct is `pub(all)`).
- 10 tests: 6 networks built + hyperparameters stored, source/target networks start distinct, deterministic with noise_std=0, noise clipped to action range, hidden state evolves, TD target shape + finiteness, zero rewards + γ=0 → zero target, tau=1 → actor_target exactly matches actor, tau=0 → actor_target unchanged, tau=1 → critics also soft-updated.


## Recent additions (v0.58.0 - v0.155.0, 2026-10-05)

Sixteen batches shipped between v0.57.0 and v0.155.0, all `moon check`
clean (0 errors). Tests are blocked by the Windows CreateProcessW 32K
command-line limit on `moon test`: the package has grown from 511 to 998
`.mbt` files, so the limit is now hit harder than when it first
appeared past v0.61.0. Verification runs through `moon check` plus the
two harnesses in the Verification harness section below; the package
holds 1939 test blocks across 383 test files and 77 examples.

> **Superseded.** The root package can no longer host a test target at all,
> and no test lives there any more. Every `*_test.mbt` is in one of nine
> sub-packages under `tests/`, each an `is-test` package importing the
> library `for "test"`, and CI runs all nine:
>
> ```sh
> moon test ./tests/waveN --target native
> ```
>
> That is 1884 tests passing, against the 280 CI could reach before. A guard
> in `publish.yml` now fails the build if a `*_test.mbt` reappears in the
> root, because such a file would be dead rather than red. See
> `tools/split_tests/README.md`.

### Batch C follow-up (v0.58.0 - v0.61.0): BPTT boundary handling
- `sample_seq_batch_at` helper for arbitrary-window sequence replay
  buffer sampling with `terminal_mask` for proper hidden-state
  zeroing at episode boundaries.
- 10 BPTT boundary tests added at v0.61.0 (1801 → 1811 tests).

### Batch D (v0.61.0 - v0.63.0): Recurrent deterministic actor-critic (LSTM)
- v0.61.0 `LSTMDeterministicPolicy`: recurrent actor for POMDPs.
  state → MLP w1 → ReLU → LSTM cell → MLP w2 → tanh squash → action.
- v0.62.0 `LSTMQNetworkContinuous`: recurrent critic.
  [state; action] → MLP w1 → ReLU → LSTM cell → MLP w2 → scalar Q.
- v0.63.0 `LSTM_DDPG`: full DDPG agent wrapping actor + twin critics +
  5 Polyak targets. 6 distinct seeds (offset by +100UL for targets).

### Batch E (v0.64.0 - v0.67.0): BPTT-driven recurrent critic/actor updates
- v0.64.0 `gru_ddpg_update_critic_seq`: GRU critic T-step BPTT.
- v0.65.0 `gru_ddpg_update_actor_seq`: GRU actor BPTT (finite-difference
  ∂Q/∂a with eps=1e-3).
- v0.66.0 `lstm_ddpg_update_critic_seq`: LSTM critic T-step BPTT.
- v0.67.0 `lstm_ddpg_update_actor_seq`: LSTM actor BPTT.

### Batch F (v0.68.0 - v0.71.0): Recurrent SAC (stochastic actor + twin critics)
- v0.68.0 `GRUSACActor`: stochastic actor — state → MLP w1 → ReLU →
  GRU cell → two MLP branches (mean + log_std) → tanh squash + Gaussian
  sampling. log_std clamped [-20, 2] (Haarnoja 2018).
- v0.69.0 `LSTMSACActor`: parallel LSTM variant.
- v0.70.0 `SAC_GRU`: stochastic actor + twin recurrent critics + auto-alpha
  + 5 Polyak targets. `target_entropy = -action_dim` default.
- v0.71.0 `SAC_LSTM`: parallel.

### Batch G (v0.72.0 - v0.75.0): Gated Transformer-XL (GTrXL) recurrent memory
- v0.72.0 `GTrXLBlock`: gated FFN residual primitive.
  y = sigmoid(W_gate · x) ⊙ FFN(x) + x (Parisotto et al. 2020).
  Gate init std scaled by 0.1 so initial gate ≈ sigmoid(0) ≈ 0.5.
- v0.73.0 `GTrXLDeterministicPolicy`: recurrent actor with GTrXL block
  as memory.
- v0.74.0 `GTrXLQNetworkContinuous` + `GTrXL_DDPG`: twin critics + 5
  Polyak targets; 9 tensors per network, 45 total.
- v0.75.0 `GTrXLSACActor` + `SAC_GTrXL`: stochastic SAC + auto-alpha; 12
  tensors per target, 60 total.

### Batch H (v0.76.0 - v0.79.0): Decision Transformer (offline RL via sequence modeling)
- v0.76.0 `DTEmbeddings`: 3-modality tokenization (rtg/state/action)
  + timestep lookup. Per-token =
  Linear_rtg(R_t) + Linear_state(s_t) + Linear_action(a_t) + Embed_ts(t).
- v0.77.0 `DecisionTransformer`: full DT = DTEmbeddings + GTrXLBlock
  backbone + action head.
- v0.78.0 `TrajectoryBuffer`: offline RL replay + returns-to-go
  (R_t = r_t + γ·R_{t+1}, terminal zeros bootstrap) +
  `sample_trajectory_window` for window batching.
- v0.79.0 `DecisionTransformerTrainer`: MSE loss + per-element gradient
  + SGD step on action head only. Embeddings + backbone frozen (BPTT
  through GTrXL deferred).

### Batch I (v0.80.0 - v0.83.0): Trajectory Transformer (planning via conditional sequence modeling)
- v0.80.0 `TTEmbeddings` + `TrajectoryTransformer`: 4-modality
  (state/action/reward/done) + 4 prediction heads (next_state,
  reward, done, value) on GTrXL backbone.
- v0.81.0 `BeamSearchPlanner`: K-beam expansion via TT dynamics +
  uniform-random action sampling + value scoring + top-K selection
  over horizon H. Score = accumulated discounted reward + γ^t · value(t).
- v0.82.0 `TrajectoryTransformerTrainer`: total MSE = MSE(next_state) +
  MSE(reward) + MSE(done) + MSE(value); SGD on all 4 heads.
- v0.83.0 `TrajectoryTransformerAgent`: full pipeline — append
  transition → sliding history → `beam_search_plan` → return best
  first action.

### Batch J (v0.85.0 - v0.88.0): time-series forecasting
- v0.85.0 `ARIMA` enhancements: Yule-Walker stationarity estimate,
  stateful `fit`/`forecast` (forecasts can be chained), confidence
  intervals.
- v0.86.0 `LSTMForecaster`: full BPTT + SGD training over a sliding
  window.
- v0.87.0 `GRUForecaster`: the same interface over a GRU, deliberately
  parallel to v0.86 so the two forecasters are interchangeable.
- v0.88.0 `TimeSeriesEnsemble`: inverse-variance weighted average of
  the ARIMA / LSTM / GRU forecasts.

### Batch K (v0.89.0 - v0.92.0): Neural ODE
- v0.89.0 `ODEFunc`: the dynamics field `f(t, x)` as a first-class
  primitive, so an ODE model is anything that supplies one.
- v0.90.0 `ODESolver`: fixed-step Euler and Heun integrators.
- v0.91.0 `AdjointSensitivity`: adjoint-method gradients, i.e. O(1) in
  trajectory length rather than O(T) BPTT.
- v0.92.0 `NeuralODEAgent`: forward + adjoint backward + SGD, closing
  the loop on the batch.

### Batch L (v0.93.0 - v0.96.0): meta-learning
- v0.93.0 `TaskSampler`: MAML task family + meta-batched sampling.
- v0.94.0 `MAML`: the FOMAML variant.
- v0.95.0 `FOMAML`: the single-inner-step form split out on its own.
- v0.96.0 `Reptile`: interpolation-based meta-update (no second
  derivative, so it composes with any inner optimiser).

### Batch M (v0.97.0 - v0.100.0): causal inference
- v0.97.0 `LinearGaussianSCM`: DAG + structural equations + sampling.
- v0.98.0 `DoCalculus`: interventions + Monte Carlo ATE.
- v0.99.0 `CounterfactualReasoning`: Pearl's three-step procedure.
- v0.100.0 `CausalEffectEstimator`: ATE + CATE + bootstrap CI.

### Batch N (v0.101.0 - v0.104.0): normalizing flows
- v0.101.0 `AffineCouplingLayer`: the Real NVP core block.
- v0.102.0 `RealNVP`: stacked coupling + fixed permutation.
- v0.103.0 `Glow`: Real NVP + ActNorm + 1x1 convolution permutation.
- v0.104.0 `NormalizingFlowModel`: the full flow with a prior and an
  exact density.

### Batch O (v0.105.0 - v0.108.0): variational autoencoders
- v0.105.0 `ELBO`: the Evidence Lower Bound loss.
- v0.106.0 `GaussianReparameterization`: the reparameterization trick.
- v0.107.0 `VAE`: the full model.
- v0.108.0 `IWAE`: the importance-weighted bound (a tighter ELBO).

### Batch P (v0.109.0 - v0.112.0): Vision Transformer
- v0.109.0 `PatchEmbedding`: the patch linear projection.
- v0.110.0 `ViTBlock`: LayerNorm + MHA + residual, then LayerNorm +
  MLP + residual.
- v0.111.0 `ViT`: PatchEmbed + CLS token + positional encoding +
  N blocks + head.
- v0.112.0 `ViTTrainer`: cross-entropy + SGD on the classification head.

### Batch Q (v0.113.0 - v0.116.0): diffusion models
- v0.113.0 `DiffusionSchedule`: linear and cosine beta schedules,
  `q_sample`, `q_sample_pair`.
- v0.114.0 `ScoreNetwork`: MLP denoiser + sinusoidal time embedding.
- v0.115.0 `DDPM`: schedule + score net + reverse sampling + loss.
- v0.116.0 `DDPMTrainer`: forward + MSE loss logging.

### Batch R (v0.117.0 - v0.120.0): energy-based models
- v0.117.0 `EnergyFunction`: MLP scalar energy.
- v0.118.0 `LangevinSampler`: finite-difference gradient + Langevin
  step + chain.
- v0.119.0 `EBM`: energy + score + Langevin sampling.
- v0.120.0 `EBMTrainer`: denoising score matching.

### Batch S (v0.121.0 - v0.124.0): DCGAN
- v0.121.0 `DCGANGenerator`: latent -> upconv stack -> tanh.
- v0.122.0 `DCGANDiscriminator`: strided conv + LeakyReLU + spectral
  norm helper.
- v0.123.0 `DCGAN`: the composite, with BCE D/G losses and the
  adversarial step.
- v0.124.0 `DCGANTrainer`: mini-batch adversarial round + eval
  metrics.

### Batch T (v0.125.0 - v0.128.0): WGAN-GP
- v0.125.0 Fix: the DCGAN generator emitted 16x16; corrected to the
  canonical 4 -> 8 -> 16 -> 32 upconv progression.
- v0.126.0 `WCritic`: Wasserstein critic loss + Lipschitz weight
  clipping.
- v0.127.0 `GradientPenalty`: WGAN-GP interpolation + finite-difference
  input gradient + the `||grad|| - 1` squared term.
- v0.128.0 `WGANTrainer`: n_critic loop + Wasserstein/GP losses + eval.

### Batch U (v0.129.0 - v0.132.0): metric learning
- v0.129.0 `EmbeddingNet`: MLP embedding + L2 normalize + pairwise
  and cosine distances.
- v0.130.0 `ContrastiveLoss`: contrastive + triplet + hard-negative +
  N-pair.
- v0.131.0 `SiameseNet`: shared-weight twin/triplet embedding.
- v0.132.0 `SiameseTrainer`: triplet batch builder + hard-negative step
  + Recall@K.

### Batch V (v0.133.0 - v0.136.0): capsule networks
- v0.133.0 Capsule primitives: squash activation, norm, dot, margin
  loss.
- v0.134.0 `PrimaryCapsule`: shared-conv primary capsule bank.
- v0.135.0 `DynamicRouting`: iterative agreement-based routing with
  coefficients.
- v0.136.0 `CapsuleNetwork`: ReLUConv + PrimaryCapsule + routing +
  margin loss.

### Batch W (v0.137.0 - v0.140.0): graph neural networks, forward pass
- v0.137.0 `Graph`: COO edge list + scatter sum/mean/max + GCN
  adjacency normalisation.
- v0.138.0 `MessagePassing`: `GCNLayer` + `MPnnLayer` (the Gilmer
  formulation).
- v0.139.0 `GraphAttention`: GAT single head + the multi-head stack
  with ELU.
- v0.140.0 `GraphClassifier`: mean/sum/max readout + MLP head +
  graph-level cross-entropy and accuracy.

### Batch X (v0.141.0 - v0.144.0): graph architectures
- v0.141.0 `GIN`: unweighted sum aggregation + learnable-eps self term
  + 2-layer MLP.
- v0.142.0 `PNA`: mean/max/min/std + degree scaler. Also fixed a
  negative-seed bug in `scatter_max` and added `scatter_min`.
- v0.143.0 `Set2Set`: LSTM-query attention readout, plus
  `Set2SetClassifier` with GIN/PNA backbone dispatch.
- v0.144.0 `EdgeGNN` (`RelationalConv` with edge features) + `H2GCN`
  separated ego/neighbour/high-order spaces + homophily diagnostics.

### Batch Y (v0.145.0 - v0.148.0): graph backward pass
- v0.145.0 `graph_backward.mbt`: scatter sum/mean backward (edge-list
  gradient accumulation) + max/min argmax routing + `GraphLinearGrad`
  + `graph_linear_backward` + SGD step.
- v0.146.0 GIN + MPNN backward: `GINLayerGrad` / `MPnnGrad` bundles and
  edge-list gradient routing.
- v0.147.0 GCN backward: `gcn_layer_support` shared with the forward,
  backward through the normalised edge weights,
  `graph_cross_entropy_grad`.
- v0.148.0 PNA backward including the standard-deviation reducer
  derivative, `TrainableGraphNet` dispatch, and
  `graph_net_train_step` (node-level and graph-level CE).

### Batch Z (v0.149.0): gradient-check harness
Central-difference checks for dL/dh and dL/dW, plus a negative control,
and they immediately found four real defects that `moon check` and code
review had both passed: the four backward stack traversals ran forwards
instead of backwards; `g.feat_dim` was used where `layer.in_dim` was
meant (in the GIN, MPNN and PNA forward AND backward); the PNA reducer
gradients were written one node at a time into a `[n_nodes x dim]`
buffer, keeping only the last node; and `GraphLinearGrad::zero`
aliased every row of `d_w` onto row 0.

### Batch AA (v0.150.0 - v0.151.0): gate green
- v0.150.0 Replaced the cross-entropy objective with a bounded
  quadratic one (`0.5 * sum(out^2)`, whose gradient is exactly `out`),
  and absolute tolerance with a relative one. This overturned the
  previous round's conclusion that GIN was the worst architecture: GIN's
  *relative* error was in fact the smallest of all six configs.
- v0.151.0 Added `gradcheck_over_fraction`, the kink-versus-bug
  discriminator (a ReLU kink perturbs 1-2 components, a wrong routing
  perturbs nearly all of them), and fixed the last two defects: the
  MPNN ReLU mask was taken on the `self_pre` branch alone instead of on
  the sum, and a leftover `let dim = g.feat_dim` in
  `pna_layer_backward`.

### Batch AB (v0.152.0 - v0.153.0): parameter gradients and the GAT head
- v0.152.0 `dL/dW` wired into the gate via
  `trainable_net_param_grad_from_dout` and `gradcheck_param_relative`,
  with a `corrupt~` factor so the negative control is real (a corrupted
  gradient produces max_diff 2490 against a tolerance of 74.8). This
  matters because dL/dW accumulates over NODES inside the linear
  backward while dL/dh accumulates over EDGES outside it, so one can
  pass while the other fails.
- v0.153.0 `gat_backward.mbt`: the GAT head backward. GAT is the only
  reducer here whose score reads BOTH endpoints of an edge, so the
  score gradient fans out to two ends and the family needed two new
  scatter directions (`scatter_add_by_src` / `scatter_add_by_dst`).

### Batch AC (v0.154.0 - v0.155.0): multi-head GAT and end-to-end training
- v0.154.0 Multi-head `GraphAttention` backward: the concatenated
  upstream gradient has to be un-interleaved per head, all heads of a
  layer share their input so their input gradients sum, and the ELU sits
  BETWEEN a head and the next layer, so its derivative multiplies at the
  head's own pre-activation. `gat_layer_backward`'s `elu` flag had been a
  placeholder, i.e. a hidden head asking for the mask silently received
  an unmasked gradient. Three latent bugs in the never-called stack
  forward were fixed at the same time: `GraphAttention::new` gave layer
  `l > 0` an input width of `dims[l]` when concatenation requires
  `num_heads * dims[l]`; a single-layer stack ran layer 0 twice; and the
  head output was indexed with the layer's `in_dim` as its row stride
  instead of its own `out_dim`. The gate separated them cleanly: the
  configuration where `in_dim == out_dim` passed and the one where they
  differ was off by 118%.
- v0.155.0 `gnn_train_demo.mbt`: an end-to-end transductive
  two-community fixture, a backbone + `GraphLinear` head composite
  training step, training curves, and the baselines needed to make the
  result mean something. It found a defect that had shipped for six
  versions: `graph_cross_entropy` had an inverted sign on its
  `logf(sum_exp)` term, so it returned a NEGATIVE loss for every
  confident and correct prediction. Its gradient was correct throughout,
  so training worked and accuracy rose; only the reported loss moved the
  wrong way. It survived because the gradient gate deliberately checks a
  bounded quadratic objective instead of cross-entropy, and a gate that
  exercises one objective cannot see a bug in another. `graph_cross_entropy`
  is fixed and `gradcheck_ce_consistency` now pins the loss against its
  own gradient.

### Batch AD (v0.156.0 - v0.159.0): `moon test`, and DragonNet

These four shipped in v0.159.0.

- v0.156.0 The training demo grows a second, deeper backbone and reports
  per-architecture depth effects: both depths learn on 5/5 probes, the
  deeper one is better on 4/5, and neither generalises on noise.
- v0.157.0 **Test sub-packages.** `moon test` on the root package has
  failed since v0.61.0 with `CreateProcessW: The filename or extension is
  too long` -- Win32 ERROR_FILENAME_EXCED_RANGE, rendered in Chinese by a
  zh-CN console -- which is the 32767-character command-line limit. The
  stage is unambiguous:
  `moon check` over all 516 files reports **0 errors**, so `moonc` is
  fine, and a sub-package that generates **957 KB** of C compiles and
  links where the root package's **203 KB** test C does not. The size of
  the generated C is therefore NOT the mechanism, and reasoning from it
  sends you down the wrong path. The fix is to stop asking the root
  package to host tests: a per-directory `moon.pkg` with
  `import { "riantr/snn_mbt", }` and `options("is-test": true)` gives a
  split package blackbox access to the package under test as
  `@snn_mbt.X`, with no `moon.work` and no interface regeneration
  (`moon work` manages *separate modules*, each with its own
  `moon.mod`). `tests/wave1/` holds 27 files and **216 test blocks that
  had never been executed before**. Those 27 files were relocated rather
  than added, so the published archive grows by 6,340 bytes (+0.5%) --
  just the `@snn_mbt.` prefixes -- and extracting the published zip and
  running `moon test ./tests/wave1` gives 216/216 against the published
  artifact rather than the working tree.
- v0.158.0 `ELU`: `elu_forward` / `elu_backward`. Only a GAT-scoped ELU
  *derivative* existed (`gat_elu_grad`); there was no forward at all. The
  backward takes the pre-activation, because at zero the two branches
  coincide and reading the post-activation would make the mask depend on
  a value the caller may already have transformed.
- v0.159.0 `DragonNet`: the architecture of arXiv:1906.02120 (Shi, Blei &
  Veitch) with the three-term objective transcribed from the authors'
  implementation (`claudiashi57/dragonnet`, `src/experiment/models.py`):
  `L_reg + L_bce + ratio * L_tarreg`. The gate in `tests/wave2/` checks
  **every** parameter by central difference rather than a sample, which
  is what the `dragonnet_flatten` / `unflatten` / `grad_flatten` /
  `layer_offset` family exists for. It caught two defects that
  type-check, compile, train, and report a plausible number:
  1. The propensity is clipped to `[1e-7, 1-1e-7]` exactly as Keras'
     `binary_crossentropy` does, but the gradient was taken from the
     clipped value as though the clip were not there -- terms of order
     **1e7** for a term the loss does not depend on, with the finite
     difference reading exactly **0**. `dn_prop` now returns
     `(value, derivative)` and the derivative is zero whenever the clip
     is active; it is the single place the clipping decision is made, so
     the loss and its gradient cannot disagree about it.
  2. A widely circulated variant of this Keras model omits the sigmoid
     on the propensity head. Without it the linear output leaves
     `[0,1]`, the clip saturates, the BCE term becomes a **constant**,
     and the propensity head trains on nothing at all while the loss
     falls and the ATE looks fine. `DragonNet::new` defaults to
     `t_sigmoid = true`; the linear variant remains available and its
     gradient stays correct either way.

  The gate also carries two negative controls (doubling and sign-flipping
  `d_epsilon` must both be detected) and a shuffled-outcome control on the
  training test, so a gate that stops comparing cannot report PASS. The
  first version of that control zeroed the y1 head's parameters instead
  of shuffling the outcomes; the targeted-regularisation term drags y0
  down uniformly, so the "blinded" model reported an ATE of 2.18 and the
  control failed for a reason unrelated to the leak it was meant to
  detect.

### Batch AE (v0.160.0 - v0.163.0): TARNet

These four shipped in v0.163.0.

Note on the name: TARNet here is **not** the treatment-effect regressor of
Shalit et al. (2017) that this package implements elsewhere. It is Terraf &
Iraqi, ICME 2026, arXiv:2605.07735 -- a temporal-aware multi-scale architecture
for closed-set speaker identification.

- v0.160.0 **TARNet primitives and forward.** `tarnet_conv1d.mbt` (dilated
  depthwise convolution, PReLU, pointwise 1x1 in `[n, C, T]` layout),
  `tarnet_se.mbt` (the post-MFA squeeze-excitation and the paper's
  multi-scale channel-time gate, CTSE), `tarnet_asp.mbt` (attentional
  statistics pooling), and `tarnet.mbt` (the residual block, the three parallel
  dilation stacks, and the classifier head). The gLN reuses this package's
  `LayerNorm(c, 1, T)` and the bottleneck 1x1 reuses `linear_forward` with the
  frame axis as the batch axis.

  Two composition faults were found here that type-check, train, and report a
  plausible number:
  1. The 1x1 convolutions inside a block read the wrong permutation of the
     `[n, C, T]` layout. Per-coordinate finite differences flagged 75 of 120
     input coordinates and the directional derivative was off by 108%.
     `tn_conv1x1_*` now exists specifically so the layout-correct pair cannot
     be confused with the `linear_*` pair.
  2. `layer_norm`'s `d_gamma` accumulated `dy * x_centered` and dropped the
     `inv_std`. Isolated reproduction: 51 of 64 coordinates wrong. It is
     invisible on any fixture whose variance is near 1, since `inv_std ~ 1`
     there -- see the `batch_norm2d` entry below for the same mistake made a
     second time.

- v0.161.0 **Block and stack backward**, plus the multi-fixture sweep that the
  rest of the package now uses.

  The last red gate was block coordinate 145, which disagreed by 11.6% on one
  fixture. It is a float32 artifact, and the argument is worth recording
  because it is the basis of the instrument used everywhere after it: a wiring
  fault is VALUE-INDEPENDENT, because the flat index -> parameter mapping does
  not change when the RNG produces different numbers. Sweeping eight fixtures
  gave 718 coordinate checks and exactly one failure -- seed 71, index 145 --
  while a mutation that really is a wiring fault missed at index 50 on six of
  eight. The tolerance was not widened; the gate gained coverage and a
  discriminator instead (at most two coordinate-fixture artifacts, and no index
  failing on three or more fixtures).

  The supporting measurement: with `f ~ 28.23`, one float32 ulp is 1.9e-6 and
  the measured slope is quantised in steps of `ulp/h = 4.77e-4`. At the base
  point the two one-sided slopes differ by exactly 8 ulps, and `d_pos` stays
  frozen bit-for-bit across steps while `d_neg` drifts -- curvature moves both
  toward one limit, so that is a kink, not curvature. Below `h = 1e-3` the
  signal is 1-10 ulps and carries no information at all.

- v0.162.0 **Model-level backward**, `tarnet_backward.mbt`. The flat gradient
  is in `tarnet_flatten`'s order, term for term, so a layout off-by-one cannot
  hide. The gate sweeps the whole 4417-parameter vector over four fixtures.

  Two more real bugs, and the first is a good illustration of how a broken
  backward and a broken loss look identical:
  1. Cross-entropy summed probabilities instead of their log (`loss -= p`
     where it had to be `loss -= logf(p)`). `d_logits` was CORRECT throughout,
     because it is computed from `p`, while the loss was wrong -- so 102 of 512
     coordinates failed and the whole thing read as a broken backward pass. What
     settled it was differentiating the WRONG objective by hand: the finite
     difference gave 0.08617, and `-p_y(p_y - onehot)/n` is 0.0862. A finite
     difference that lands exactly on a *different* analytic formula is
     measuring a different function, not disagreeing with the gradient.
  2. `batch_norm2d_backward`'s `d_gamma` was missing the `inv_std` factor --
     the same mistake as v0.160.0's `layer_norm` one, in a second function. On
     TARNet's `bn_seg` the small-variance channels made it a ~250x
     under-estimate. It survived because every existing gate checked `d_gamma`'s
     SHAPE and never its value, while the `d_input` gate used a single channel.
     `tests/wave1/batch_norm2d_test.mbt` now has a numerical gate on a fixture
     whose channels are scaled 1.0 / 0.05 / 20.0, plus an assertion that the
     channel spread really exceeds 20x -- otherwise the gate would be green
     because `inv_std` is 1 and there is nothing to see.

  `se_backward` (the plain squeeze-excitation, not the CTSE) is new here. Its
  first gate was vacuous: deleting the pooling path ENTIRELY left it green at
  90/90, because `SeParam::new` scales `w1`/`w2` by 0.05 and the squeeze is
  then ~0.25% of the direct path. Amplifying the fixture to ~13% made it catch
  both the deletion (35 of 90) and a missing `g*(1-g)` (58 of 58).

- v0.163.0 **Training, and the log-Mel front end.** `tarnet_train.mbt` reuses
  the existing `adam_update_arrays` by passing the flat parameter vector as one
  weight array, so the optimizer is the same already-gated code the rest of the
  package uses. 60 steps take the loss from 0.6388 to 0.00061 with zero error
  on a separable batch.

  Two gate lessons from the training test, both recorded next to the gate:
  1. "Every parameter moved" was the wrong assertion. It demanded 90% of the
     vector and failed at 3742/4359 -- which was correct information, not a bug:
     617 parameters have exactly zero gradient (dead PReLU slopes, biases on
     dead channels) and must legitimately stay put. The assertion is now the
     property: every parameter with a non-zero gradient must move, and at least
     one must be dead or the fixture has changed.
  2. **A training gate can never stand in for the numerical gates.** Adam's
     update is `lr * m_hat / (sqrt(v_hat) + eps)`, so rescaling the gradient by
     any constant `k` rescales `m_hat` by `k` and `sqrt(v_hat)` by `|k|` and
     they cancel. Measured: halving the gradient moved the 60-step loss from
     0.0006082716 to 0.0006083312 with every gate green. What the training gate
     does catch is the wiring the numerical gates cannot -- a stale gradient, an
     update to a discarded copy, a reported post-update loss.

  `tarnet_logmel.mbt` is the front end, forward-only because the reference
  computes it under `torch.no_grad()`. New primitives: radix-2 FFT, HTK mel
  scale and filterbank, Hamming window, reflect padding, STFT, pre-emphasis.

  Every gate above is SELF-CONSISTENCY: it compares my code against my own
  arithmetic, so a transcription error in the ARCHITECTURE would pass all of
  them. A line-by-line reading of the reference against this implementation is
  the only check that can catch that class, and it found two things:

  1. **Pre-emphasis was missing entirely.** The reference's model front end is
     `PreEmphasis(0.97)` -> `MelSpectrogram`, and `train_voxceleb.py` defaults
     `feature_type` to `'raw'`, so the dataset hands the model a WAVEFORM and
     the model does its own feature extraction -- pre-emphasis included. The
     reference implements it as `pad(1, 0, reflect)` then `conv1d` with kernel
     `[-coef, 1.0]`, which is
     `y[0] = x[0] - coef*x[1]` and `y[n] = x[n] - coef*x[n-1]`. The reflected
     FIRST sample is the detail: borrowing `x[0]` instead of `x[1]` yields
     `(1-coef)*x[0] = 0.03*x[0]`, which is not a rounding difference but a
     different filter. Mutation-tested -- with the wrong reflection the gate
     reports `y[0] = 0.030` against a correct `-0.940`.
  2. A comment on the classifier claimed the reference uses
     `xavier_uniform` while the code allocated `randn(0.05)`. A comment that
     states something the code does not do is worse than no comment, so the
     mismatch is now resolved in favour of saying what the code does, with the
     deviation (0.068 vs 0.05 gain for the default shape) and the reason.

  Everything else matched, and the comparison is worth recording because the
  close calls are easy to get wrong: SE sits INSIDE the MFA module (conv ->
  ReLU -> SE), its bottleneck is `max(32, fuse_out // 4)`, the dilation stacks
  are `build_stack([1,2], 4) / build_stack([4,8], 4) / build_stack([16], 4)`
  with the repeat loop OUTSIDE the dilation loop, `bn_seg` is over
  `2 * fuse_out`, and the block is `pw1 -> PReLU -> gLN -> depthwise -> PReLU ->
  gLN` then `res_conv` then CTSE then `x + residual`.

  One deviation remains, documented in the code: the reference declares TWO
  separate `nn.PReLU()` layers per block, each learning its own slope, while
  this implementation shares one.

  3. **The gLN affine was per element where asteroid's is per channel.** The
     reference gets its normalisation from `norms.get("gLN")`, which is
     `asteroid.masknn.norms.GlobLN` -- an UNDECLARED dependency: it is not in
     the reference's own `install_requires`, so it cannot be read from the
     checkout and has to be verified against the library. Its `_LayerNorm`
     allocates `gamma`/`beta` of shape `(channel_size,)` and broadcasts them
     over time, while the statistic is still one mean and one std over every
     dim except batch, i.e. the `(C, T)` plane jointly. This implementation had
     used this package's `LayerNorm(c, 1, T)`, which agrees on the axis but
     allocates one gamma/beta per ELEMENT. Two consequences, only one of which
     is cosmetic:
       - parameter count: `GlobLN` has C, `LayerNorm` had C * T. At the default
         `t_frames = 301` that is 301x more parameters in each of a block's
         four norms, none of them in the reference.
       - **LENGTH.** `GlobLN` has no length-dependent parameter, so the
         reference runs utterances of any length through ONE model.
         `LayerNorm(c, 1, T)` cannot -- the model was hard-wired to the
         `t_frames` it was constructed with, which defeats the point of a
         model whose input is variable-length speech. `tarnet_forward` read `t`
         from the input, not from the model, and `tarnet_param_count` no longer
         depends on `t` at all.

     `GlobalLayerNorm` (`layer_norm.mbt`) is the faithful version, with EPS
     1e-8 against `LayerNorm`'s 1e-5, because that is asteroid's value on the
     gLN path. `tarnet_test.mbt`'s round-trip gate had hard-coded the old flat
     offsets (`2 * 6 * 9`, `b.hid * 9 * 2`); they are now derived from the
     model, since a literal would have kept pointing at the wrong slot while
     still compiling. The gate that matters is the length-independence one: the
     parameter count must be identical at every `t`, because a forward that
     merely happens to run at several lengths proves nothing while a per-frame
     affine is still hiding somewhere in the parameter vector. Making the
     count `t`-dependent again fails it with `t = 9 gives 3665 against 3593`. The reference's
  unstated torchaudio defaults are written out rather than left implicit
  (`center=True` with reflect padding, `power=2.0`, `mel_scale='htk'`,
  `norm=None`, and a SYMMETRIC window whose cosine runs over N-1). The `1e-6`
  goes in BEFORE the log: it floors the power spectrum, so a silent frame logs
  finitely, rather than clamping the log and biasing loud frames. Resampling is
  a precondition, not a step -- the reference uses torchaudio's Kaiser-windowed
  sinc kernel, which is not reproduced here, so the input must already be mono
  at 16 kHz.

  Two assertions in the filterbank gate were wrong and measurement caught both.
  "Every band peaks at 1.0" is impossible: with `f_min = 20 Hz` the FFT bins sit
  every 31.25 Hz while band 0's centre is 42.2 Hz, so the triangle falls between
  bins. "Peak alignment improves with frequency" is also false -- the measured
  peaks oscillate across the whole bank (band 1 = 0.443, band 2 = 0.392) and
  never exceed 0.51, because at low frequency a mel band is narrower than a
  single bin. Neither is a defect; both follow from the reference's parameters.
  The gate now asserts only what holds for every correct bank, and leaves "does
  this frequency land in the right band" to a pure-tone test.

### Batch AF (v0.164.0 - v0.165.0): gate-design and BatchNorm defects

Two defects that belong to the TARNet batch above but landed after Batch AE
was written.

- v0.164.0 **`batch_norm2d`'s cache recorded statistics the forward never
  used.** The forward picks its statistics by `bn.training`, and in inference
  mode two things went wrong: the cache stored the BATCH `inv_std` (computed
  unconditionally and never overwritten) while the output was written with the
  RUNNING one, and the two coupling terms in `d_input` were applied
  unconditionally even though running statistics are constants and the output
  is affine in the input. So a backward after an inference-mode forward
  differentiated a relationship the forward never used. No gate caught it
  because **no gate set `training = false`**. The fix stores the statistics
  actually used and adds a `coupled` flag to `BatchNormCache`; the new gate
  freezes running stats at values deliberately unlike the batch's, and
  dropping the one line that stores the effective `inv_std` fails it with
  48 of 48 coordinates out of tolerance, worst 148.9.

- v0.165.0 **The block sweep's artifact budget was an absolute count, and
  that was a design defect rather than a tuning choice.** The number of checks
  is the block's flat length over a stride derived from that length, and the
  flat length changed when the gLN affine went from per-element to
  per-channel -- so the same budget of 2 meant 0.28% of 718 checks before and
  0.33% of 600 after, and the sweep had drifted to sitting exactly AT its own
  limit. A budget whose meaning moves with the thing it measures is not a
  budget. It is now `TN_MAX_ARTIFACT_FRACTION = 0.01`. Mutation-tested: a 10%
  scale on the gradient entering `ln1` yields 89 of 600 artifacts (14.8%) and
  trips the per-index rule on all eight fixtures, so real faults are still
  rejected loudly.

### Batch AG (v0.166.0): ASP's context layout, and the BatchNorm1d behind it

Two real defects, both found by building a PyTorch **autograd** oracle rather than
another finite difference, and both in code that several gates already passed.

- **`asp_forward` wrote the context's x-slot at x's stride.** `ctx` is `[n, 3c,
  t]`; the mean and sd slots used that stride and the x-slot used `[n, c, t]`'s.
  Batch *b*'s x-slot therefore lands on batch *b-1*'s mean-slot. At n=3, c=5,
  t=7 batch 1's x overwrote batch 0's mean at `[35,70)` and batch 2's x
  overwrote batch 0's sd at `[70,105)`. Batch 0's attention was reading other
  samples' activations as its own context, and its "standard deviation" slot
  held raw waveform values -- including negatives, which no square root can
  produce. `asp_backward` was meanwhile reading the x-slot at the *correct*
  stride, so the forward and the backward disagreed about the buffer's layout.

  The write was self-consistent (`ctx[i0] = x[i0]` reads and writes one index),
  which is why nothing downstream complained. It is the third stride defect of
  this family in this module and the same failure mode as the two already
  recorded in Batch AD.

- **`batch_norm2d_forward` computed its variance one-pass**, as `E[x^2] -
  E[x]^2`. Exact in real arithmetic and exact in float64, but in float32 it
  subtracts two nearly equal numbers whenever the mean is large relative to the
  spread. On a fixture with per-channel means over `[-7.5, 7.5]` and variances
  spanning 1535x, channel 0's variance came out 2.09% high and its `inv_std`
  1.00% high. It is now PyTorch's two-pass form, `E[(x - mean)^2]`.

  It hid because every pre-existing test normalises inputs that are either
  already centred or have a spread comparable to their mean, where the
  cancellation costs six figures rather than two.

**Why neither gate caught either.** `asp_backward` carried a long investigation
ending in NOT SOLVED and the claim that "the chain written in `asp_backward` is
not the chain `asp_forward` computes" -- which was literally true and was one
index. Every measurement in it was a finite difference of the module's own
forward, and a difference of a forward is blind to a forward that is wrong: it
reports the gradient of whatever the forward computes. The gate that replaced the
removed one runs at `rtol=3e-2`, `atol=2e-3`, `h=1.2e-2` and is green -- but a
float32 central difference of an objective summed over ~170 terms was measured
carrying an absolute noise floor of order 0.1 at a 1e-2 step, so that gate sits
below its own instrument's noise.

The replacement is `tests/wave2/asp_oracle_test.mbt`, which compares against
`verify/asp_oracle.py` -- torch autograd, no step size, exact to float32
roundoff, structurally unable to agree with a wrong implementation by
construction. It checks the forward, `d_x`, all four convolution gradients,
`d_b2` (exactly zero, because softmax is shift-invariant), the BatchNorm affine
gradients, and the shape of the BatchNorm's own reduction.

With `d_x` verified, **ASP's `nn.BatchNorm1d(att_hidden)` is now implemented**
(`nn/batch_norm1d.mbt`, a checked `h=1` view of `batch_norm2d`, and the layer
itself between ReLU and tanh in `tarnet_asp.mbt`). Its per-channel affine is
carried in `AspParam` and threaded through `tarnet_flatten` / `tarnet_backward`;
leaving it out would have produced a normalisation that is correct in every
forward check and permanently frozen at `gamma = 1, beta = 0`.

Every tolerance in both gates is derived rather than fitted -- the forward's
absolute bound is `2*(FLT_EPS/2)*max|x|*max(inv_std)*sqrt(n*w)`, the gradient's
is 2.5x the measured float32 residue, and the finite-difference column's is set
by what the instrument can resolve (its worst error on the *correct*
implementation is 13% at the best step). `verify/gate_canary.py` runs all of it
against a scratch copy and requires each gate to fail when its defect is
restored.

