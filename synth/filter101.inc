/* SPDX-License-Identifier: GPL-3.0-only */

/* Prewarped bilinear integrator coefficients, tan(pi * f / 44100),
 * with f from 10 Hz to 20 kHz, uniformly spaced in log frequency. */
static const float filter_warp[65] = {
    0.000712379f, 0.000802214f, 0.000903376f, 0.001017296f, 0.001145581f, 0.001290044f,
    0.001452724f, 0.001635919f, 0.001842216f, 0.002074528f, 0.002336135f, 0.002630733f,
    0.002962482f, 0.003336066f, 0.003756762f, 0.004230511f, 0.004764003f, 0.005364775f,
    0.006041312f, 0.006803169f, 0.007661109f, 0.008627253f, 0.009715251f, 0.010940477f,
    0.012320250f, 0.013874073f, 0.015623922f, 0.017594548f, 0.019813844f, 0.022313236f,
    0.025128148f, 0.028298510f, 0.031869353f, 0.035891471f, 0.040422190f, 0.045526246f,
    0.051276793f, 0.057756579f, 0.065059313f, 0.073291271f, 0.082573213f, 0.093042671f,
    0.104856743f, 0.118195533f, 0.133266456f, 0.150309728f, 0.169605483f, 0.191483174f,
    0.216334240f, 0.244629525f, 0.276943741f, 0.313990600f, 0.356674515f, 0.406168722f,
    0.464036950f, 0.532429639f, 0.614413676f, 0.714554767f, 0.840010828f, 1.002749329f,
    1.224517316f, 1.549600314f, 2.084680557f, 3.169898008f, 6.798795776f
};

static float filter_coefficient(float position)
{
    float address = clamp(position, 0.0f, 1.0f) * 64.0f;
    unsigned index = (unsigned)address;
    float g = filter_warp[index];
    if (index < 64)
        g += (filter_warp[index + 1u] - g) * (address - (float)index);
    return g / (1.0f + g);
}

/* Same logarithmic grid below the original 10-Hz origin. The optional base
 * range reaches 3.6 Hz; positive positions retain the original lookup exactly. */
#define TG_FILTER_LOW_SPAN 0.13441183503801085f
static float filter_low_coefficient(float position)
{
    if (position >= 0.0f) return filter_coefficient(position);
    static const float low_warp[10] = {
        0.000712379f, 0.000632605185f, 0.000561764307f, 0.000498856388f,
        0.000442993075f, 0.000393385490f, 0.000349333101f, 0.000310213820f,
        0.000275475224f, 0.000244626752f
    };
    float address = clamp(-position, 0.0f, TG_FILTER_LOW_SPAN) * 64.0f;
    unsigned index = (unsigned)address;
    float g = low_warp[index] + (low_warp[index+1u] - low_warp[index]) * (address-(float)index);
    return g / (1.0f + g);
}

/* Effective resonance/drive curves measured against the software reference.
 * The shared output-level reference keeps oscillator and filter-only sounds
 * balanced; these coefficients are not claims about a particular circuit. */
static float filter_resonance(float amount)
{
    return amount * (3.809825880f + 1.783801242f * amount);
}

static float filter_input_gain(float amount)
{
    return 1.0f + amount * (1.048740356f + 0.486853388f * amount);
}

static float filter_feedback(float value, float *slope)
{
    /* Symmetric bounded feedback, with unity small-signal gain.
     * This is a smooth approximation to limiting in the feedback path. */
    const float limit = 0.451313315f;
    float x = clamp(value / limit, -1.0f, 1.0f);
    *slope = 1.0f - x * x;
    return limit * x * (1.0f - (x * x) / 3.0f);
}

static float filter_tick(float state[4], float input, float G, float k)
{
    /* All stages share the cutoff. The final stage is G^4 * u + memory.
     * Solve the current-sample feedback before advancing any integrator. */
    float G2 = G * G;
    float G4 = G2 * G2;
    float memory = (1.0f - G) * (((state[0] * G + state[1]) * G + state[2]) * G + state[3]);
    float drive = memory + G4 * input;
    float y = drive / (1.0f + G4 * k);
    float slope;
    for (unsigned i = 0; i < 2; i++) {
        float feedback = filter_feedback(y, &slope);
        y -= (y + G4 * k * feedback - drive) / (1.0f + G4 * k * slope);
    }
    float signal = input - k * filter_feedback(y, &slope);
    for (unsigned i = 0; i < 4; i++) {
        float delta = (signal - state[i]) * G;
        signal = state[i] + delta;
        state[i] = signal + delta;
    }
    return signal;
}
