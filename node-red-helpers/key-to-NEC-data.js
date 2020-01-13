switch(msg.payload) {
    case "ON"     :    msg.payload = "2FD48B7"; break;
    case "MUTE"   :    msg.payload = "2FD08F7"; break;
    case "KEY1"   :    msg.payload = "2FD807F"; break;
    case "KEY2"   :    msg.payload = "2FD40BF"; break;
    case "KEY3"   :    msg.payload = "2FDC03F"; break;
    case "KEY4"   :    msg.payload = "2FD20DF"; break;
    case "KEY5"   :    msg.payload = "2FDA05F"; break;
    case "KEY6"   :    msg.payload = "2FD609F"; break;
    case "KEY7"   :    msg.payload = "2FDE01F"; break;
    case "KEY8"   :    msg.payload = "2FD10EF"; break;
    case "KEY9"   :    msg.payload = "2FD906F"; break;
    case "KEY0"   :    msg.payload = "2FD00FF"; break;
    case "SOURCE" :    msg.payload = "2FD28D7"; break;
    case "UP"     :    msg.payload = "2FDB847"; break;
    case "DOWN"   :    msg.payload = "2FDB847"; break;
    case "LEFT"   :    msg.payload = "2FD42BD"; break;
    case "RIGHT"  :    msg.payload = "2FD02FD"; break;
    case "OK"     :    msg.payload = "2FD847B"; break;
    case "VUP"    :    msg.payload = "2FD58A7"; break;
    case "VDOWN"  :    msg.payload = "2FD7887"; break;
    case "CUP"    :    msg.payload = "2FDD827"; break;
    case "CDOWN"  :    msg.payload = "2FDF807"; break;
  }
  
  return msg;