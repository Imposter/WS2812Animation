// WS2812 Animation Lib
// Copyright (C) 2017 Eyaz Rehman. All Rights Reserved.

#include <Adafruit_NeoPixel.h>
#include <TrueRandom.h>
#include <limits.h>

#define DEBUG
#define DEBUG_BAUD_RATE 115200
#define DEBUG_TIMEOUT 50

#define CHANNEL_START_PIN 4
#define CHANNEL_COUNT 2
#define CHANNEL_BRIGHTNESS 8
#define CHANNEL_MAX_SIZE 40

#define CHANNEL_VDROP 60 //mV
#define CHANNEL_VDEV 40 // mV
#define CHANNEL_SAMPLES 10

#define ANIMATION_MAX_COLORS 16
#define ANIMATION_MIN_SPEED 0.1f
#define ANIMATION_MAX_SPEED 0.75f

// From provideyourown.com/2012/secret-arduino-voltmeter-measure-battery-voltage
static long ReadVCC() {
  // Read 1.1V reference against AVcc
  // Set the reference to Vcc and the measurement to the internal 1.1V reference
  #if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
    ADMUX = _BV(MUX5) | _BV(MUX0);
  #elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
    ADMUX = _BV(MUX3) | _BV(MUX2);
  #else
    ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #endif  

  delay(2); // Wait for VREF to settle
  ADCSRA |= _BV(ADSC); // Start conversion
  while (bit_is_set(ADCSRA,ADSC)); // Measuring

  uint8_t low  = ADCL; // Must read ADCL first - it then locks ADCH  
  uint8_t high = ADCH; // Unlocks both

  long result = (high << 8) | low;

  result = 1125300L / result; // Calculate VCC (in mV); 1125300 = 1.1*1023*1000
  return result; // VCC in millivolts
}

class Channel {
  static const uint8_t DefaultType = NEO_GRB + NEO_KHZ800;
  
  Adafruit_NeoPixel mPixels;
  uint16_t mLedCount;

public:
  Channel()
    : mPixels(0, 0, DefaultType), mLedCount(0) {}

  Channel(uint8_t pin, uint8_t maxSize, uint8_t type = DefaultType)
    : mPixels(maxSize, pin, type), mLedCount(0) {}

  void Initialize() {
    // Initialize pixels and set to max brightness
    mPixels.begin();
    mPixels.show();

    uint8_t brightness = mPixels.getBrightness();
    mPixels.setBrightness(255);

    long vInitial = 0;
    for (uint8_t i = 0; i < CHANNEL_SAMPLES; i++)
      vInitial += ReadVCC();
    vInitial /= CHANNEL_SAMPLES;

#ifdef DEBUG
    Serial.print("vInitial = ");
    Serial.print(vInitial);
    Serial.println("mV");
#endif

    for (uint16_t i = 0; i < mPixels.numPixels(); i++) {
      // Turn on led 
      mPixels.setPixelColor(i, 0xFFFFFF);
      mPixels.show();
  
      // Sample
      long vLed = 0;
      for (int j = 0; j < CHANNEL_SAMPLES; j++)
        vLed += ReadVCC();
      vLed /= CHANNEL_SAMPLES;
  
      // Check if the voltage is within the threshold limit of the WS2812 pixel power usage
      long vDiff = abs(vInitial - vLed);
      if ((vDiff <= CHANNEL_VDROP && vDiff >= CHANNEL_VDROP - CHANNEL_VDEV) 
        || (vDiff >= CHANNEL_VDROP && vDiff <= CHANNEL_VDROP + CHANNEL_VDEV))
        mLedCount++;
    
#ifdef DEBUG
      Serial.print("vLed ");
      Serial.print(i);
      Serial.print(" = ");
      Serial.print(vLed);
      Serial.print("mV (vDiff = ");
      Serial.print(vInitial - vLed);
      Serial.println("mV)");
#endif
  
      // Turn off led 
      mPixels.setPixelColor(i, 0x0);
      mPixels.show();
    }

    // Restore brightness
    mPixels.setBrightness(brightness);

#ifdef DEBUG
    Serial.print("LedCount = ");
    Serial.println(mLedCount);
#endif
  }

  void SetPin(uint8_t pin) {
    mPixels.setPin(pin);
  }

  void SetMaxLedCount(uint16_t count) {
    if (mLedCount > 0) {
      return;
    }
    
    mLedCount = 0;
    mPixels.updateLength(count);
  }

  void SetBrightness(uint8_t limit) {
    mPixels.setBrightness(limit);
  }

  const uint16_t &GetLedCount() const {
    return mLedCount;
  }

  void SetLedColor(uint16_t i, uint32_t color) {
    if (i >= mLedCount) {
      return;
    }
    
    mPixels.setPixelColor(i, color);
  }

  void SetLedColor(uint16_t i, uint8_t r, uint8_t g, uint8_t b) {
    if (i >= mLedCount) {
      return;
    }
    
    mPixels.setPixelColor(i, r, g, b);
  }

  void Show() {
    mPixels.show();
  }

  void Clear() {
    for (uint16_t i = 0; i < mLedCount; i++)
      mPixels.setPixelColor(i, 0);
    mPixels.show();
  }
};

class IAnimation {
public:
  virtual void Initialize(Channel *channels) = 0;
  virtual void Animate() = 0;
};

struct Color {
  uint8_t Red;
  uint8_t Green;
  uint8_t Blue;
};

class SnakeAnimation : public IAnimation {
  static const float BodyLengthFactor = 0.25f;
  static const uint32_t UpdateSpeedMax = 50;
  static const float UpdateSpeed = ANIMATION_MIN_SPEED;
  
  struct AnimationData {
    Channel *Channel;
    
    uint16_t HeadLocation;
    float BodyLength;
    
    Color Colors[ANIMATION_MAX_COLORS];
    uint8_t CurrentColor;
    boolean Decreasing;
    
    float Speed;
    uint32_t LastAnimation;
  };

  AnimationData mData[CHANNEL_COUNT];

public:
  void Initialize(Channel *channels) override {
    for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
      Channel *channel = &channels[i];
      AnimationData &data = mData[i];

      // Create snake
      data.Channel = channel;
      data.HeadLocation = 0;
      data.BodyLength = BodyLengthFactor;
      data.Decreasing = false;
      data.Speed = UpdateSpeed;

      // Generate one color
      data.Colors[0].Red = TrueRandom.random(0, 255);
      data.Colors[0].Green = TrueRandom.random(0, 255);
      data.Colors[0].Blue = TrueRandom.random(0, 255);
      data.CurrentColor = 0;
    }
  }

  void Animate() override {    
    for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
      AnimationData &data = mData[i];

      uint32_t currentTime = millis();
      uint32_t deltaTime;
      if (currentTime < data.LastAnimation) {
        deltaTime = ULONG_MAX - data.LastAnimation + currentTime;
      } else {
        deltaTime = currentTime - data.LastAnimation;
      }
      
      if (deltaTime < (uint8_t)(UpdateSpeedMax * (1.0f - data.Speed))) {
        continue;
      }
      
      Channel *channel = data.Channel;
      Color currentColor = data.Colors[data.CurrentColor];

      // Draw snake
      uint16_t bodyLength = (uint8_t)(data.BodyLength * channel->GetLedCount());
      int16_t startPos = data.HeadLocation;
      int16_t endPos;
      int16_t increment;
      int16_t endCheck;
      int16_t resetPosition;
      if (!data.Decreasing) {
        endPos = startPos - bodyLength;
        increment = 1;
        endCheck = channel->GetLedCount() - 1;
        resetPosition = 0;
      } else {
        endPos = startPos + bodyLength;
        increment = -1;
        endCheck = 0;
        resetPosition = channel->GetLedCount() - 1;
      }

      if (endPos < 0) {
        endPos += channel->GetLedCount();
      } else if (endPos >= channel->GetLedCount()) {
        endPos -= channel->GetLedCount();
      }
  
      // Turn off previous tail
      channel->SetLedColor(endPos, 0);
  
      // Turn on head
      channel->SetLedColor(startPos, currentColor.Red, currentColor.Blue, currentColor.Green);

      // Show updates
      channel->Show();

      // Update head location
      data.HeadLocation += increment;
      if (startPos == endCheck) {
        // Reset head position
        data.HeadLocation = resetPosition;
        
        // Update color
        if (++data.CurrentColor >= ANIMATION_MAX_COLORS) {
          data.CurrentColor = 0;
        }
        
        currentColor = data.Colors[data.CurrentColor];
        if (currentColor.Red == 0 && currentColor.Green == 0 && currentColor.Blue == 0)
          data.CurrentColor = 0;
      }

      // Update last update time
      data.LastAnimation = currentTime;
    }
  }

  void SetChannelDecreasing(uint8_t channel, boolean decreasing) {
    mData[channel].Decreasing = decreasing;
  }

  const boolean &GetChannelDecreasing(uint8_t channel) const {
    return mData[channel].Decreasing;
  }

  void SetChannelColor(uint8_t channel, uint8_t i, uint8_t r, uint8_t g, uint8_t b) {
    Color &color = mData[channel].Colors[i];
    color.Red = r;
    color.Green = g;
    color.Blue = b;
  }

  void SetChannelColor(uint8_t channel, uint8_t i, Color &newColor) {
    Color &color = mData[channel].Colors[i];
    color.Red = newColor.Red;
    color.Green = newColor.Green;
    color.Blue = newColor.Blue;
  }

  void SetChannelBodyLength(uint8_t channel, float bodyLength) {
    mData[channel].BodyLength = bodyLength;
  }

  const float &GetChannelBodyLength(uint8_t channel) const {
    return mData[channel].BodyLength;
  }

  void SetChannelSpeed(uint8_t channel, float updateSpeed) {
    mData[channel].Speed = updateSpeed;
  }

  const float &GetChannelSpeed(uint8_t channel) const {
    return mData[channel].Speed;
  }
};

Channel gChannels[CHANNEL_COUNT];
SnakeAnimation gAnimation;
bool gAnimating;

void setup() {
#ifdef DEBUG
  Serial.begin(DEBUG_BAUD_RATE);
  Serial.setTimeout(DEBUG_TIMEOUT);
  Serial.println("=================================");
  Serial.println("WS2812 Animation Lib (DEBUG)");
  Serial.println("=================================");
#endif

  // Initialize channels
  for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
    Channel &channel = gChannels[i];

    channel.SetPin(CHANNEL_START_PIN + i);
    channel.SetBrightness(CHANNEL_BRIGHTNESS);
    channel.SetMaxLedCount(CHANNEL_MAX_SIZE);

    channel.Initialize();
  }

  // Initialize animation
  gAnimation.Initialize(gChannels);

  // Start animating
  gAnimating = true;
}

void loop() {
  // Animate
  if (gAnimating)
    gAnimation.Animate();
  
#ifdef DEBUG
  // Check serial for data
  if (Serial.available() > 0) {
    String line = Serial.readStringUntil(' ');
    line.trim();

    if (line.equals("start")) {
      gAnimating = true;
    } else if (line.equals("stop")) {
      gAnimating = false;
      for (uint8_t i = 0; i < CHANNEL_COUNT; i++)
        gChannels[i].Clear();
    } else if (line.equals("increase")) {
      for (uint8_t i = 0; i < CHANNEL_COUNT; i++)
        gAnimation.SetChannelDecreasing(i, false);
    } else if (line.equals("decrease")) {
      for (uint8_t i = 0; i < CHANNEL_COUNT; i++)
        gAnimation.SetChannelDecreasing(i, true);
    } else if (line.equals("genColors")) {
       for (uint8_t i = 0; i < CHANNEL_COUNT; i++)
        gAnimation.SetChannelColor(i, 0, 
          TrueRandom.random(0, 255), 
          TrueRandom.random(0, 255), 
          TrueRandom.random(0, 255));
    } else if (line.equals("genAllColors")) {
       for (uint8_t i = 0; i < CHANNEL_COUNT; i++)
        for (uint8_t j = 0; j < ANIMATION_MAX_COLORS; j++)
          gAnimation.SetChannelColor(i, j, 
            TrueRandom.random(0, 255), 
            TrueRandom.random(0, 255), 
            TrueRandom.random(0, 255));
    } else if (line.equals("setColor")) {
      int32_t channel = Serial.parseInt();
      int32_t index = Serial.parseInt();
      int32_t r = Serial.parseInt();
      int32_t g = Serial.parseInt();
      int32_t b = Serial.parseInt();

      if (channel >= 0 && index >= 0 
        && channel < CHANNEL_COUNT && index < CHANNEL_MAX_SIZE 
        && r >= 0 && g >= 0 && b >= 0
        && r <= 255 && g <= 255 && b <= 255) {
        gAnimation.SetChannelColor(channel, index, r, g, b);
      }
    } else if (line.equals("setLength")) {
      int32_t channel = Serial.parseInt();
      float bodyLength = Serial.parseFloat();

      if (channel >= 0 && bodyLength > 0.0f
        && channel < CHANNEL_COUNT && bodyLength <= 1.0f) {
        gAnimation.SetChannelBodyLength(channel, bodyLength);  
      }
    } else if (line.equals("setSpeed")) {
      int32_t channel = Serial.parseInt();
      float updateSpeed = Serial.parseFloat();

      if (channel >= 0 && updateSpeed >= ANIMATION_MIN_SPEED
        && channel < CHANNEL_COUNT && updateSpeed <= ANIMATION_MAX_SPEED) {
        gAnimation.SetChannelSpeed(channel, updateSpeed);  
      }
    }
  }
#endif
}
