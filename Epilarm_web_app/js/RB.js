//definition of Rolling Buffer that supports analysis of its contents

//'datatype' for creating circular buffer
//originally taken from https://stackoverflow.com/a/4774081 and adapted for special purposes
var createRingBuffer = function(length){
	  var pointer = 0, buffer = new Array(length);

	  return {
	    get  : function(key){
	        if (key < 0){
	            return buffer[pointer+key];
	        } else if (key === false){
	            return buffer[pointer - 1];
	        } else{
	            return buffer[key];
	        }
	    },
	    push : function(item){
	      buffer[pointer] = item;
	      pointer = (pointer + 1) % length;
	      return item;
	    },
	    prev : function(){
	        var tmp_pointer = (pointer - 1) % length;
	        if (buffer[tmp_pointer]){
	            pointer = tmp_pointer;
	            return buffer[pointer];
	        }
	    },
	    next : function(){
	        if (buffer[pointer]){
	            pointer = (pointer + 1) % length;
	            return buffer[pointer];
	        }
	    },
	    pointer : function(){
	    	return pointer;
	    },
	    stdrepr : function(){
	    	return buffer.slice(pointer).concat(buffer.slice(0,pointer));
	    },
	    fill : function(value){
	    	buffer.fill(value);
	    }
	  };
	};
