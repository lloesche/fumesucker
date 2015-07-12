# Solder Fume Extractor [![Build Status](https://travis-ci.org/lloesche/fumesucker.svg?branch=master)](https://travis-ci.org/lloesche/fumesucker)
######Arduino based Fan controller / DC motor controller
![Solder Fume Extractor](https://raw.githubusercontent.com/lloesche/fumesucker/master/misc/IMG_0535.jpg "Solder Fume Extractor")

## Introduction
One thing that regularly annoyed me when soldering was solder fumes rising up into my face blocking the view onto my components. For a while I would just hook up a PC fan to a 12V supply and have that suck the fumes away from my work space.

Now I started toying around with Arduinos lately and figured it'd be a nice little project to have an adjustable solder fume extractor with a display. Mainly because I've just started getting into displays. I also love the feel of rotary encoders so one of those had to be incorporated :-)
For the small amount of solder work I'm doing I will ignore any additional health aspects. This project is solely about getting the fumes away from my work area dispersing them into a well ventilated room. If you're soldering on a daily bases you probably know all about proper filters and ventilation. So I won't go into that.

I happened to have a bunch of ULN2003A high-voltage, high-current darlington transistor arrays laying around. So I'll be using one of those to control the fans. I also happened to have two 120mm 3-PIN PC fans on hand and the grey plastic enclosure I'm using has enough room for both of them. So I figured the system should be able to control two fans. For my use case they'll be running at the same speed but from a code point of view they could easily run at different speeds. Right now I just stacked them both, running at a fairly low speed. Around 20% duty cycle (~5V) is enough to extract the solder fumes from 10cm away.

## Build
### Overview
The build is fairly simple. It's using a single 12V supply. I chose an L7805CV linear voltage regulator to get the 12V down to 5V for use by the Arduino Pro Mini and [the PCD8544 display](http://www.banggood.com/Nokia-5110-LCD-Module-White-Backlight-For-Arduino-UNO-Mega-Prototype-p-86022.html). That display is usually 3.3V but the 3 EUR version that I received from Banggood is 5V tolerant.
According to [the official Arduino Pro Mini spec](https://www.arduino.cc/en/Main/arduinoBoardProMini) the RAW pin should be 12V tolerant. However [my cheap Chinese clones](http://www.aliexpress.com/item/With-the-bootloader-1pcs-ATMEGA328P-Pro-Mini-328-Mini-ATMEGA328-5V-16MHz-for-Arduino/32340811597.html) (1,45 EUR shipped) regularly had their voltage regulator go up in smoke when applying anything above 9V. So I opted for an external regulator which is only like 0,07 EUR apiece shipped.

![Breadboard](https://raw.githubusercontent.com/lloesche/fumesucker/master/misc/IMG_0528.jpg "Breadboard")

The ULN2003A has seven channels each rated at 500mA/50V. The 12V PC fans only use up to ~350mA each. But just to be sure and because I had enough pins to spare I combined channels 1-3 and 4-6.

### Controlling Fan Speed
Brushless DC fans like the ones I'm using prefer a constant voltage being applied to them. So ideally I'd be using a linear regulator to adjust the DC voltage across the fans or even better a DC-DC regulator so I wouldn't have to care about dissipated heat.
But as mentioned above the only thing I had readily available was my Arduino and a bunch of ULN2003A. So the only way I'd be controlling my fans would be using PWM. Now it's worth mentioning that there are 4-PIN PWM fans available and those would be best suited for the job. However the ones I had are 3-PIN ones without a dedicated PWM line.
This means we'll be controlling the fans using direct PWM. I.e. we'll turn them on and off at a certain frequency. There are some disadvantages to that approach. For starters the speed sensor (the third pin) will be rendered useless as it's being powered off the same supply voltage as the motor. I don't have any use for the speed sensor but it's something to be aware of. Secondly this approach increases stress on the fan. During the on period of the PWM cycle [current flow through the windings will be higher than normal](http://www.precisionmicrodrives.com/application-notes-technical-guides/application-bulletins/ab-021-measuring-rpm-from-back-emf) when the fans are spinning at a reduced speed. Again nothing I am particularly concerned with as the fans won't be running over long periods of time compared to their normal work environment inside a PC.

By default Pins 9 and 10, controlled by Timer 1, run at a PWM frequency of 490Hz. When I pulsed the fans at that frequency I could hear a high pitched noise. I [read that the ideal frequency](https://www.maximintegrated.com/en/app-notes/index.mvp/id/1784) would be somewhere between 20Hz and 160Hz. I tried with both 30Hz and 120Hz available on Timer 1. However both produced an audible sound (a low humming noise) with my fans. So I opted for a third solution which is to increase the PWM frequency of Timer 1 from 490Hz up to 31kHz making the pulsing inaudible.
![PWM Signal](https://raw.githubusercontent.com/lloesche/fumesucker/master/misc/IMG_0504.jpg "PWM Signal")
The document states "Using PWM ... too quickly can cause the internal commutation electronics to cease functioning correctly.". I didn't notice any such problems even after having them run overnight. Maybe it's going to have a long term effect? Maybe it depends on the fans? If you have any more details please let me know.

In summary this was the observed behaviour with my fans:
* 30Hz (low humming noise)
* 122Hz (low humming noise)
* 490Hz (high pitched noise)
* 3.9kHz (high pitched noise)
* 31kHz (no noise whatsoever)

I did observe that the voltage characteristics changed depending on the PWM frequency. At 30Hz and 1% speed setting it would measure an avg of 2.0V whereas at 31kHz and 1% speed it would measure an avg of around 3.0V.

Here are all the measurements I did at 31kHz. Notice that the percentage on the x-axis is the speed setting (int speed_percent which on the low end is offset by int min_speed), not the duty cycle.
![Voltage vs. Speed](https://raw.githubusercontent.com/lloesche/fumesucker/master/misc/voltage.png "Voltage vs. Speed")

### Rotary Encoder
To me the most challenging part of this build was the rotary encoder. Fortunately I found [this excellent tutorial](http://www.allaboutcircuits.com/projects/how-to-use-a-rotary-encoder-in-a-mcu-based-project/) detailing how to properly filter/debounce them and interpret their output. In the process I also learned about pin change interrupts. It's a really good read that I can highly recommend to anyone new to Arduinos (like myself). In the end I was left with two clean signals that were easy to interpret.

Any time the encoder shaft is turned we will receive four pin change interrupts.

##### Turning Left
![Left Turn](https://raw.githubusercontent.com/lloesche/fumesucker/master/misc/DS1Z_QuickPrint712.png "Left Turn")

*Interrupts:*
:one: ENCODER_PIN1 going LOW
:two: ENCODER_PIN2 going LOW
:three: ENCODER_PIN1 going HIGH
:four: ENCODER_PIN2 going HIGH

##### Turning Right
![Right Turn](https://raw.githubusercontent.com/lloesche/fumesucker/master/misc/DS1Z_QuickPrint713.png "Right Turn")

*Interrupts:*
:one: ENCODER_PIN2 going LOW
:two: ENCODER_PIN1 going LOW
:three: ENCODER_PIN2 going HIGH
:four: ENCODER_PIN1 going HIGH


### Finished board
I ended up using a 5x7cm layout. It fits the enclosure I have at hand just so.
![Board Front](https://raw.githubusercontent.com/lloesche/fumesucker/master/misc/IMG_0532.jpg "Board Front")
![Board Back](https://raw.githubusercontent.com/lloesche/fumesucker/master/misc/IMG_0533.jpg "Board Back")

## Contributing
If you'd like to contribute code or improve the design feel free to send pull requests, report issues or send me an Email.
